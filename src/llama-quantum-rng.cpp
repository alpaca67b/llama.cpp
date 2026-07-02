#include "llama-quantum-rng.h"
#include "llama.h"

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char * llama_quantum_random_path() {
    return "C:\\Scripts\\llama.cpp\\resources\\random_bytes_1.bin";
}

struct llama_quantum_random_state {
    std::mutex mutex;
    std::vector<uint16_t> values;
    std::vector<std::string> texts;
    std::atomic<bool> loaded = false;
    std::atomic<size_t> next_index = 0;
    std::atomic<size_t> total_consumed = 0;
};

thread_local const char * llama_quantum_random_last_text_ptr = nullptr;

llama_quantum_random_state & llama_quantum_random_get_state() {
    static llama_quantum_random_state state;
    return state;
}

void llama_quantum_random_load_locked(llama_quantum_random_state & state) {
    if (state.loaded.load(std::memory_order_relaxed)) {
        return;
    }

    const char * path = llama_quantum_random_path();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("failed to open quantum random file: ") + path);
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const size_t pair_count = bytes.size() / 2;

    if (pair_count == 0) {
        throw std::runtime_error(std::string("quantum random file has no usable byte pairs: ") + path);
    }

    state.values.reserve(pair_count);
    state.texts.reserve(pair_count);

    for (size_t i = 0; i < pair_count; ++i) {
        const uint16_t value =
            ((uint16_t) bytes[2*i] << 8) |
            (uint16_t) bytes[2*i + 1];

        state.values.push_back(value);
        state.texts.push_back(std::to_string((unsigned int) value));
    }

    state.loaded.store(true, std::memory_order_release);

    if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
        fprintf(stderr, "[quantum-rng] loaded %zu values from %s\n", state.values.size(), path);
    }
}

static void llama_quantum_random_ensure_loaded(llama_quantum_random_state & state) {
    if (state.loaded.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.mutex);
    llama_quantum_random_load_locked(state);
}

static double llama_quantum_random_value_at(llama_quantum_random_state & state, size_t index) {
    llama_quantum_random_last_text_ptr = state.texts[index].c_str();
    return (double) state.values[index] / 65536.0;
}

} // namespace

extern "C" LLAMA_API const char * llama_quantum_qrng_detect_port() {
    const char * path = llama_quantum_random_path();
    std::ifstream file(path, std::ios::binary);
    return file ? path : nullptr;
}

const char * llama_quantum_random_last_text() {
    return llama_quantum_random_last_text_ptr;
}

double llama_quantum_random_01() {
    llama_quantum_random_state & state = llama_quantum_random_get_state();

    llama_quantum_random_ensure_loaded(state);

    const size_t index = state.next_index.fetch_add(1, std::memory_order_relaxed) % state.values.size();
    const size_t consumed = state.total_consumed.fetch_add(1, std::memory_order_relaxed) + 1;

    if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
        const bool log_all = std::getenv("LLAMA_QUANTUM_RNG_LOG_ALL") != nullptr;
        if (log_all || consumed <= 64) {
            fprintf(stderr,
                    "[quantum-rng] source=virtual-bin consumed=%zu random_index=%zu value=%s\n",
                    consumed, index, state.texts[index].c_str());
        }
    }

    return llama_quantum_random_value_at(state, index);
}

double llama_quantum_random_next_01() {
    return llama_quantum_random_01();
}

void llama_quantum_qrng_shutdown() {
}
