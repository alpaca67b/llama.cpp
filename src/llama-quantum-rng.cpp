#include "llama-quantum-rng.h"
#include "llama.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
extern "C" {
#include "qcc.h"
}
#endif

namespace {

const char * llama_quantum_random_path() {
    return "C:\\Scripts\\qiskit\\full_export\\random_numbers_1920x1440.txt";
}

bool llama_quantum_qrng_disabled() {
    return std::getenv("LLAMA_QUANTUM_QRNG_DISABLE") != nullptr;
}

struct llama_quantum_csv_state {
    std::mutex mutex;
    std::vector<double> values;
    std::vector<std::string> texts;
    std::atomic<bool> loaded = false;
};

thread_local const char * llama_quantum_random_last_text_ptr = nullptr;
thread_local size_t llama_quantum_random_last_index = 0;
thread_local bool llama_quantum_random_last_index_valid = false;

#ifdef _WIN32
struct llama_quantum_qrng_state {
    std::mutex mutex;
    std::condition_variable cv_sample;
    std::thread worker;
    bool worker_started = false;
    bool stop_worker = false;
    bool init_failed = false;
    bool have_sample = false;
    uint8_t latest_bytes[3] = { 0, 0, 0 };
    uint64_t sample_seq = 0;
    uint64_t consumed_seq = 0;
    bool port_detected = false;
    std::string port_name;
    std::string error_message;
};

void llama_quantum_qrng_reset_state(llama_quantum_qrng_state & state) {
    state.worker_started = false;
    state.stop_worker = false;
    state.init_failed = false;
    state.have_sample = false;
    state.latest_bytes[0] = 0;
    state.latest_bytes[1] = 0;
    state.latest_bytes[2] = 0;
    state.sample_seq = 0;
    state.consumed_seq = 0;
    state.port_detected = false;
    state.port_name.clear();
    state.error_message.clear();
}

void llama_quantum_qrng_shutdown_impl(llama_quantum_qrng_state & state) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stop_worker = true;
    }

    state.cv_sample.notify_all();

    if (state.worker_started && state.worker.joinable()) {
        state.worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.worker = std::thread();
        llama_quantum_qrng_reset_state(state);
    }
}

bool llama_quantum_qrng_prepare_command_mode(qcc_hdl_t & qcc) {
    cmdctrl_status_t status = {};
    int status_ret = qcc_cmd_get_status(&qcc, &status);

    if (status_ret == QCC_OK) {
        return true;
    }

    if (qcc_cmd_stop(&qcc) != QCC_OK) {
        return false;
    }

    status_ret = qcc_cmd_get_status(&qcc, &status);
    return status_ret == QCC_OK;
}
#endif

struct llama_quantum_random_state {
    llama_quantum_csv_state csv;
    std::atomic<size_t> total_consumed = 0;

#ifdef _WIN32
    llama_quantum_qrng_state qrng;

    ~llama_quantum_random_state() {
        llama_quantum_qrng_shutdown_impl(qrng);
    }
#endif
};

llama_quantum_random_state & llama_quantum_random_get_state() {
    static llama_quantum_random_state state;
    return state;
}

void llama_quantum_random_load_locked(llama_quantum_csv_state & state) {
    if (state.loaded.load(std::memory_order_relaxed)) {
        return;
    }

    const char * path = llama_quantum_random_path();
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(std::string("failed to open quantum random file: ") + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t start = 0;

        while (start < line.size()) {
            size_t end = line.find(',', start);
            if (end == std::string::npos) {
                end = line.size();
            }

            std::string_view token(line.data() + start, end - start);

            while (!token.empty() && std::isspace((unsigned char) token.front())) {
                token.remove_prefix(1);
            }

            while (!token.empty() && std::isspace((unsigned char) token.back())) {
                token.remove_suffix(1);
            }

            if (!token.empty()) {
                const std::string token_text(token);
                char * parse_end = nullptr;
                const double value = std::strtod(token_text.c_str(), &parse_end);

                if (parse_end != token_text.c_str() && *parse_end == '\0' && std::isfinite(value)) {
                    state.values.push_back(value);
                    state.texts.push_back(token_text);
                }
            }

            start = end + 1;
        }
    }

    if (state.values.empty()) {
        throw std::runtime_error(std::string("quantum random file has no usable numbers: ") + path);
    }

    state.loaded.store(true, std::memory_order_release);

    if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
        fprintf(stderr, "[quantum-rng] loaded %zu values from %s\n", state.values.size(), path);
    }
}

#ifdef _WIN32
std::vector<std::string> llama_quantum_qrng_candidate_ports() {
    std::vector<std::string> ports;

    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return ports;
    }

    for (DWORD index = 0;; ++index) {
        char value_name[256];
        BYTE data[256];
        DWORD value_name_len = sizeof(value_name);
        DWORD data_len = sizeof(data);
        DWORD type = 0;

        const LSTATUS ret = RegEnumValueA(key, index, value_name, &value_name_len, NULL, &type, data, &data_len);
        if (ret == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (ret != ERROR_SUCCESS || type != REG_SZ || data_len < 2) {
            continue;
        }

        data[data_len - 1] = '\0';
        ports.emplace_back((const char *) data);
    }

    RegCloseKey(key);

    std::sort(ports.begin(), ports.end(), [](const std::string & a, const std::string & b) {
        const int a_num = a.rfind("COM", 0) == 0 ? std::atoi(a.c_str() + 3) : INT_MAX;
        const int b_num = b.rfind("COM", 0) == 0 ? std::atoi(b.c_str() + 3) : INT_MAX;
        return a_num == b_num ? a < b : a_num < b_num;
    });
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());

    return ports;
}

std::string llama_quantum_qrng_detect_port_locked(llama_quantum_qrng_state & state) {
    if (state.port_detected) {
        return state.port_name;
    }

    for (const std::string & port : llama_quantum_qrng_candidate_ports()) {
        qcc_hdl_t qcc = {};
        const int init_ret = qcc_init(&qcc, QCC_SERIAL, const_cast<char *>(port.c_str()), 500, 1760);
        if (init_ret != QCC_OK) {
            continue;
        }

        const bool status_ok = llama_quantum_qrng_prepare_command_mode(qcc);
        qcc_close(&qcc);

        if (status_ok) {
            state.port_name = port;
            state.port_detected = true;
            return state.port_name;
        }
    }

    return "";
}

void llama_quantum_qrng_worker(llama_quantum_qrng_state * state) {
    std::string port;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        port = llama_quantum_qrng_detect_port_locked(*state);
    }

    if (port.empty()) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "failed to detect QRNG COM port";
        }

        state->cv_sample.notify_all();
        return;
    }

    qcc_hdl_t qcc = {};
    const int ret = qcc_init(&qcc, QCC_SERIAL, const_cast<char *>(port.c_str()), 500, 1760);

    if (ret != QCC_OK) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "qcc_init(" + port + ") failed with code " + std::to_string(ret);
        }

        state->cv_sample.notify_all();
        return;
    }

    if (!llama_quantum_qrng_prepare_command_mode(qcc)) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "failed to stop stale QRNG continuous mode on " + port;
        }

        state->cv_sample.notify_all();
        qcc_close(&qcc);
        return;
    }

    const int start_ret = qcc_cmd_start(&qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
    if (start_ret != QCC_OK) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "qcc_cmd_start(continuous) failed with code " + std::to_string(start_ret);
        }

        state->cv_sample.notify_all();
        qcc_close(&qcc);
        return;
    }

    for (;;) {
        uint8_t bytes[3] = { 0, 0, 0 };
        const int read_ret = qcc_read_continuous(&qcc, bytes, 3);

        if (read_ret != QCC_OK) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->stop_worker) {
                state->init_failed = true;
                state->error_message = "qcc_read_continuous(3 bytes) failed with code " + std::to_string(read_ret);
            }
            state->cv_sample.notify_all();
            break;
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->latest_bytes[0] = bytes[0];
            state->latest_bytes[1] = bytes[1];
            state->latest_bytes[2] = bytes[2];
            state->have_sample = true;
            state->error_message.clear();
            ++state->sample_seq;
        }

        state->cv_sample.notify_all();

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stop_worker) {
                break;
            }
        }
    }

    qcc_cmd_stop(&qcc);
    qcc_close(&qcc);
}

void llama_quantum_qrng_start_if_needed(llama_quantum_qrng_state & state) {
    std::lock_guard<std::mutex> lock(state.mutex);

    if (state.worker_started) {
        return;
    }

    state.worker = std::thread(llama_quantum_qrng_worker, &state);
    state.worker_started = true;
}

uint32_t llama_quantum_qrng_next_u32(llama_quantum_qrng_state & state) {
    std::unique_lock<std::mutex> lock(state.mutex);

    if (state.init_failed) {
        throw std::runtime_error(state.error_message);
    }

    state.cv_sample.wait(lock, [&]() {
        return state.stop_worker || state.init_failed || (state.have_sample && state.sample_seq != state.consumed_seq);
    });

    if (state.init_failed) {
        throw std::runtime_error(state.error_message);
    }

    if (state.stop_worker) {
        throw std::runtime_error("QRNG worker stopped");
    }

    const uint32_t value =
        (uint32_t) state.latest_bytes[0] |
        ((uint32_t) state.latest_bytes[1] << 8) |
        ((uint32_t) state.latest_bytes[2] << 16);

    state.consumed_seq = state.sample_seq;

    return value;
}

size_t llama_quantum_pick_index(llama_quantum_qrng_state & state, size_t n_values) {
    if (n_values == 0) {
        throw std::runtime_error("quantum random csv table is empty");
    }

    const uint32_t value = llama_quantum_qrng_next_u32(state);
    const uint64_t scaled = (uint64_t) value * n_values;

    return (size_t) (scaled / (1u << 24));
}
#endif

} // namespace

extern "C" LLAMA_API const char * llama_quantum_qrng_detect_port() {
#ifdef _WIN32
    if (llama_quantum_qrng_disabled()) {
        return nullptr;
    }

    llama_quantum_random_state & state = llama_quantum_random_get_state();
    std::lock_guard<std::mutex> lock(state.qrng.mutex);
    llama_quantum_qrng_detect_port_locked(state.qrng);
    return state.qrng.port_name.empty() ? nullptr : state.qrng.port_name.c_str();
#else
    return nullptr;
#endif
}

const char * llama_quantum_random_last_text() {
    return llama_quantum_random_last_text_ptr;
}

static double llama_quantum_random_value_at(llama_quantum_random_state & state, size_t index) {
    llama_quantum_random_last_index = index;
    llama_quantum_random_last_index_valid = true;
    llama_quantum_random_last_text_ptr = state.csv.texts[index].c_str();
    return state.csv.values[index];
}

static void llama_quantum_random_ensure_loaded(llama_quantum_csv_state & state) {
    if (state.loaded.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.mutex);
    llama_quantum_random_load_locked(state);
}

double llama_quantum_random_01() {
    llama_quantum_random_state & state = llama_quantum_random_get_state();
    static std::atomic<size_t> fallback_index = 0;

    llama_quantum_random_ensure_loaded(state.csv);

#ifdef _WIN32
    const bool use_qrng = !llama_quantum_qrng_disabled();

    if (use_qrng) {
        llama_quantum_qrng_start_if_needed(state.qrng);
    }

    const size_t index = use_qrng
        ? llama_quantum_pick_index(state.qrng, state.csv.values.size())
        : fallback_index.fetch_add(1) % state.csv.values.size();

    const size_t consumed = ++state.total_consumed;

    if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
        const bool log_all = std::getenv("LLAMA_QUANTUM_RNG_LOG_ALL") != nullptr;
        if (log_all || consumed <= 64) {
            fprintf(stderr,
                    "[quantum-rng] source=%s consumed=%zu random_index=%zu value=%s\n",
                    use_qrng ? "qrng" : "txt",
                    consumed, index, state.csv.texts[index].c_str());
        }
    }

    return llama_quantum_random_value_at(state, index);
#else
    const size_t index = fallback_index.fetch_add(1) % state.csv.values.size();
    return llama_quantum_random_value_at(state, index);
#endif
}

double llama_quantum_random_next_01() {
    llama_quantum_random_state & state = llama_quantum_random_get_state();

    llama_quantum_random_ensure_loaded(state.csv);

    if (!llama_quantum_random_last_index_valid) {
        return llama_quantum_random_01();
    }

    const size_t index = (llama_quantum_random_last_index + 1) % state.csv.values.size();
    return llama_quantum_random_value_at(state, index);
}

void llama_quantum_qrng_shutdown() {
#ifdef _WIN32
    llama_quantum_qrng_shutdown_impl(llama_quantum_random_get_state().qrng);
#endif
}
