#include "llama-quantum-rng.h"
#include "llama.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cstdarg>
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
static constexpr size_t LLAMA_QUANTUM_QRNG_SAMPLE_CAPACITY        = 30000;
static constexpr size_t LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_INITIAL  = 300;
static constexpr size_t LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_FALLBACK = 200;
static constexpr size_t LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_RETRY    = 150;
static constexpr size_t LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_MIN      = 100;
static constexpr size_t LLAMA_QUANTUM_QRNG_STARTUP_FILL           = 3000;
static constexpr size_t LLAMA_QUANTUM_QRNG_LOW_WATERMARK          = 6000;
static constexpr size_t LLAMA_QUANTUM_QRNG_TARGET_FILL            = 24000;

bool llama_quantum_qrng_io_log_enabled() {
    return std::getenv("LLAMA_QUANTUM_QRNG_IO_LOG") != nullptr;
}

void llama_quantum_qrng_io_log(const char * fmt, ...) {
    if (!llama_quantum_qrng_io_log_enabled()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[quantum-qrng-io] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

struct llama_quantum_qrng_state {
    std::mutex mutex;
    std::condition_variable cv_sample;
    std::thread worker;
    bool worker_started = false;
    bool worker_already_started_logged = false;
    bool stop_worker = false;
    bool init_failed = false;
    std::array<uint32_t, LLAMA_QUANTUM_QRNG_SAMPLE_CAPACITY> samples = {};
    uint64_t write_seq = 0;
    uint64_t read_seq = 0;
    bool read_seq_initialized = false;
    size_t batch_samples = LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_INITIAL;
    size_t startup_fill = LLAMA_QUANTUM_QRNG_STARTUP_FILL;
    bool port_detected = false;
    std::string port_name;
    std::string error_message;
};

void llama_quantum_qrng_reset_state(llama_quantum_qrng_state & state) {
    state.worker_started = false;
    state.worker_already_started_logged = false;
    state.stop_worker = false;
    state.init_failed = false;
    state.samples.fill(0);
    state.write_seq = 0;
    state.read_seq = 0;
    state.read_seq_initialized = false;
    state.batch_samples = LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_INITIAL;
    state.startup_fill = LLAMA_QUANTUM_QRNG_STARTUP_FILL;
    state.port_detected = false;
    state.port_name.clear();
    state.error_message.clear();
}

void llama_quantum_qrng_shutdown_impl(llama_quantum_qrng_state & state) {
    llama_quantum_qrng_io_log("shutdown requested worker_started=%d stop_worker=%d\n",
            state.worker_started ? 1 : 0,
            state.stop_worker ? 1 : 0);

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stop_worker = true;
    }

    state.cv_sample.notify_all();

    if (state.worker_started && state.worker.joinable()) {
        llama_quantum_qrng_io_log("joining worker thread\n");
        state.worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.worker = std::thread();
        llama_quantum_qrng_reset_state(state);
    }

    llama_quantum_qrng_io_log("shutdown complete\n");
}

bool llama_quantum_qrng_prepare_command_mode(qcc_hdl_t & qcc) {
    cmdctrl_status_t status = {};
    int status_ret = qcc_cmd_get_status(&qcc, &status);
    llama_quantum_qrng_io_log("qcc_cmd_get_status ret=%d\n", status_ret);

    if (status_ret == QCC_OK) {
        return true;
    }

    const int stop_ret = qcc_cmd_stop(&qcc);
    llama_quantum_qrng_io_log("qcc_cmd_stop ret=%d after status failure\n", stop_ret);
    if (stop_ret != QCC_OK) {
        return false;
    }

    status_ret = qcc_cmd_get_status(&qcc, &status);
    llama_quantum_qrng_io_log("qcc_cmd_get_status retry ret=%d\n", status_ret);
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

    constexpr size_t max_txt_values = 65536;

    const char * path = llama_quantum_random_path();
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(std::string("failed to open quantum random file: ") + path);
    }

    std::string line;
    while (state.values.size() < max_txt_values && std::getline(file, line)) {
        size_t start = 0;

        while (state.values.size() < max_txt_values && start < line.size()) {
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
        llama_quantum_qrng_io_log("using cached port=%s\n", state.port_name.c_str());
        return state.port_name;
    }

    const std::vector<std::string> ports = llama_quantum_qrng_candidate_ports();
    llama_quantum_qrng_io_log("detected %zu candidate COM ports\n", ports.size());

    for (const std::string & port : ports) {
        qcc_hdl_t qcc = {};
        llama_quantum_qrng_io_log("probing port=%s qcc_init(timeout_ms=500 read_size=1760)\n", port.c_str());
        const int init_ret = qcc_init(&qcc, QCC_SERIAL, const_cast<char *>(port.c_str()), 500, 1760);
        llama_quantum_qrng_io_log("probe port=%s qcc_init ret=%d\n", port.c_str(), init_ret);
        if (init_ret != QCC_OK) {
            continue;
        }

        const bool status_ok = llama_quantum_qrng_prepare_command_mode(qcc);
        llama_quantum_qrng_io_log("probe port=%s prepare_command_mode=%s\n", port.c_str(), status_ok ? "ok" : "fail");
        llama_quantum_qrng_io_log("probe port=%s qcc_close\n", port.c_str());
        qcc_close(&qcc);

        if (status_ok) {
            state.port_name = port;
            state.port_detected = true;
            llama_quantum_qrng_io_log("selected port=%s\n", state.port_name.c_str());
            return state.port_name;
        }
    }

    llama_quantum_qrng_io_log("no usable QRNG COM port detected\n");
    return "";
}

uint64_t llama_quantum_qrng_oldest_seq_locked(const llama_quantum_qrng_state & state) {
    return state.write_seq > LLAMA_QUANTUM_QRNG_SAMPLE_CAPACITY
        ? state.write_seq - LLAMA_QUANTUM_QRNG_SAMPLE_CAPACITY
        : 0;
}

size_t llama_quantum_qrng_available_locked(const llama_quantum_qrng_state & state) {
    return (size_t) (state.write_seq - llama_quantum_qrng_oldest_seq_locked(state));
}

size_t llama_quantum_qrng_desired_fill_locked(const llama_quantum_qrng_state & state) {
    return state.read_seq_initialized ? LLAMA_QUANTUM_QRNG_TARGET_FILL : state.startup_fill;
}

size_t llama_quantum_qrng_next_batch_samples(size_t current_batch_samples) {
    if (current_batch_samples > LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_FALLBACK) {
        return LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_FALLBACK;
    }

    if (current_batch_samples > LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_RETRY) {
        return LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_RETRY;
    }

    if (current_batch_samples > LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_MIN) {
        return LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_MIN;
    }

    return current_batch_samples;
}

uint32_t llama_quantum_qrng_pack_u24(const uint8_t * bytes) {
    return
        (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16);
}

void llama_quantum_qrng_worker(llama_quantum_qrng_state * state) {
    llama_quantum_qrng_io_log("worker thread started\n");

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

        llama_quantum_qrng_io_log("worker failed to detect QRNG COM port\n");
        state->cv_sample.notify_all();
        return;
    }

    qcc_hdl_t qcc = {};
    llama_quantum_qrng_io_log("opening port=%s qcc_init(timeout_ms=500 read_size=1760)\n", port.c_str());
    const int ret = qcc_init(&qcc, QCC_SERIAL, const_cast<char *>(port.c_str()), 500, 1760);
    llama_quantum_qrng_io_log("worker port=%s qcc_init ret=%d\n", port.c_str(), ret);

    if (ret != QCC_OK) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "qcc_init(" + port + ") failed with code " + std::to_string(ret);
        }

        llama_quantum_qrng_io_log("worker init failed port=%s ret=%d\n", port.c_str(), ret);
        state->cv_sample.notify_all();
        return;
    }

    llama_quantum_qrng_io_log("worker port=%s prepare_command_mode\n", port.c_str());
    if (!llama_quantum_qrng_prepare_command_mode(qcc)) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "failed to stop stale QRNG continuous mode on " + port;
        }

        llama_quantum_qrng_io_log("worker port=%s prepare_command_mode failed\n", port.c_str());
        state->cv_sample.notify_all();
        llama_quantum_qrng_io_log("worker port=%s qcc_close\n", port.c_str());
        qcc_close(&qcc);
        return;
    }

    llama_quantum_qrng_io_log("worker port=%s qcc_cmd_start(mode=continuous)\n", port.c_str());
    const int start_ret = qcc_cmd_start(&qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
    llama_quantum_qrng_io_log("worker port=%s qcc_cmd_start ret=%d\n", port.c_str(), start_ret);
    if (start_ret != QCC_OK) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->init_failed = true;
            state->error_message = "qcc_cmd_start(continuous) failed with code " + std::to_string(start_ret);
        }

        llama_quantum_qrng_io_log("worker port=%s continuous start failed ret=%d\n", port.c_str(), start_ret);
        state->cv_sample.notify_all();
        llama_quantum_qrng_io_log("worker port=%s qcc_close\n", port.c_str());
        qcc_close(&qcc);
        return;
    }

    std::array<uint8_t, LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_INITIAL * 3> batch_bytes = {};
    std::array<uint32_t, LLAMA_QUANTUM_QRNG_BATCH_SAMPLES_INITIAL> batch_values = {};

    for (;;) {
        size_t batch_samples = 0;
        size_t available_before = 0;
        size_t desired_fill = 0;

        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv_sample.wait(lock, [&]() {
                return state->stop_worker || state->init_failed || llama_quantum_qrng_available_locked(*state) < llama_quantum_qrng_desired_fill_locked(*state);
            });

            if (state->stop_worker || state->init_failed) {
                break;
            }

            available_before = llama_quantum_qrng_available_locked(*state);
            desired_fill = llama_quantum_qrng_desired_fill_locked(*state);
            batch_samples = state->batch_samples;
        }

        llama_quantum_qrng_io_log("fill request port=%s available=%zu desired=%zu batch_samples=%zu bytes=%zu\n",
                port.c_str(),
                available_before,
                desired_fill,
                batch_samples,
                batch_samples * 3);

        llama_quantum_qrng_io_log("qcc_read_continuous port=%s bytes=%zu\n", port.c_str(), batch_samples * 3);
        const int read_ret = qcc_read_continuous(&qcc, batch_bytes.data(), (int) (batch_samples * 3));
        llama_quantum_qrng_io_log("qcc_read_continuous port=%s ret=%d\n", port.c_str(), read_ret);

        if (read_ret != QCC_OK) {
            bool retry_with_smaller_batch = false;
            size_t next_batch_samples = batch_samples;

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->stop_worker) {
                    next_batch_samples = llama_quantum_qrng_next_batch_samples(state->batch_samples);

                    if (next_batch_samples < state->batch_samples) {
                        state->batch_samples = next_batch_samples;
                        state->startup_fill = (std::min)(state->startup_fill, state->batch_samples);
                        state->error_message.clear();
                        retry_with_smaller_batch = true;
                    } else {
                        state->init_failed = true;
                        state->error_message = "qcc_read_continuous(" + std::to_string(batch_samples * 3) + " bytes) failed with code " + std::to_string(read_ret);
                    }
                }
            }

            state->cv_sample.notify_all();

            if (retry_with_smaller_batch) {
                llama_quantum_qrng_io_log("fill retry port=%s failed_bytes=%zu ret=%d next_batch_samples=%zu\n",
                        port.c_str(),
                        batch_samples * 3,
                        read_ret,
                        next_batch_samples);
                if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
                    fprintf(stderr,
                            "[quantum-rng] qcc_read_continuous(%zu bytes) failed with code %d, reducing batch to %zu samples\n",
                            batch_samples * 3, read_ret, next_batch_samples);
                }
                continue;
            }

            llama_quantum_qrng_io_log("fill failed port=%s failed_bytes=%zu ret=%d\n",
                    port.c_str(),
                    batch_samples * 3,
                    read_ret);
            break;
        }

        for (size_t i = 0; i < batch_samples; ++i) {
            batch_values[i] = llama_quantum_qrng_pack_u24(batch_bytes.data() + i*3);
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stop_worker) {
                break;
            }

            for (size_t i = 0; i < batch_samples; ++i) {
                state->samples[(size_t) (state->write_seq % LLAMA_QUANTUM_QRNG_SAMPLE_CAPACITY)] = batch_values[i];
                ++state->write_seq;
            }

            state->error_message.clear();

            llama_quantum_qrng_io_log("fill committed port=%s samples=%zu available_before=%zu available_after=%zu write_seq=%llu read_seq=%llu\n",
                    port.c_str(),
                    batch_samples,
                    available_before,
                    llama_quantum_qrng_available_locked(*state),
                    (unsigned long long) state->write_seq,
                    (unsigned long long) state->read_seq);
        }

        state->cv_sample.notify_all();
    }

    llama_quantum_qrng_io_log("worker port=%s qcc_cmd_stop\n", port.c_str());
    qcc_cmd_stop(&qcc);
    llama_quantum_qrng_io_log("worker port=%s qcc_close\n", port.c_str());
    qcc_close(&qcc);
    llama_quantum_qrng_io_log("worker thread exiting port=%s\n", port.c_str());
}

void llama_quantum_qrng_start_if_needed(llama_quantum_qrng_state & state) {
    std::lock_guard<std::mutex> lock(state.mutex);

    if (state.worker_started) {
        if (!state.worker_already_started_logged) {
            llama_quantum_qrng_io_log("worker already started\n");
            state.worker_already_started_logged = true;
        }
        return;
    }

    llama_quantum_qrng_io_log("starting worker thread\n");
    state.worker = std::thread(llama_quantum_qrng_worker, &state);
    state.worker_started = true;
    state.worker_already_started_logged = false;
}

uint32_t llama_quantum_qrng_next_u32(llama_quantum_qrng_state & state) {
    std::unique_lock<std::mutex> lock(state.mutex);

    for (;;) {
        if (state.init_failed) {
            throw std::runtime_error(state.error_message);
        }

        if (state.stop_worker) {
            throw std::runtime_error("QRNG worker stopped");
        }

        const uint64_t oldest_seq = llama_quantum_qrng_oldest_seq_locked(state);
        const uint64_t newest_seq = state.write_seq;
        const size_t available = (size_t) (newest_seq - oldest_seq);

        if (!state.read_seq_initialized) {
            if (available >= state.startup_fill && available > 0) {
                state.read_seq = oldest_seq;
                state.read_seq_initialized = true;
            }
        } else if (state.read_seq < oldest_seq) {
            state.read_seq = oldest_seq;
        }

        if (state.read_seq_initialized && state.read_seq < newest_seq) {
            const uint32_t value = state.samples[(size_t) (state.read_seq % LLAMA_QUANTUM_QRNG_SAMPLE_CAPACITY)];
            ++state.read_seq;

            if ((size_t) (newest_seq - state.read_seq) <= LLAMA_QUANTUM_QRNG_LOW_WATERMARK) {
                state.cv_sample.notify_all();
            }

            return value;
        }

        state.cv_sample.wait(lock);
    }
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
