#include "llama-quantum-rng.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char * llama_quantum_random_path() {
    return "C:\\Scripts\\qiskit\\full_export\\random_numbers_1920x1440.txt";
}

struct llama_quantum_random_state {
    std::mutex mutex;
    std::vector<double> values;
    size_t index = 0;
    size_t total_consumed = 0;
    bool loaded = false;
};

llama_quantum_random_state & llama_quantum_random_get_state() {
    static llama_quantum_random_state state;
    return state;
}

void llama_quantum_random_load_locked(llama_quantum_random_state & state) {
    if (state.loaded) {
        return;
    }

    const char * path = llama_quantum_random_path();
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(std::string("failed to open quantum random file: ") + path);
    }

    while (!file.eof()) {
        double value;
        if (file >> value) {
            if (std::isfinite(value)) {
                value = std::fmod(value, 1.0);
                if (value < 0.0) {
                    value += 1.0;
                }
                if (value >= 1.0) {
                    value = std::nextafter(1.0, 0.0);
                }
                state.values.push_back(value);
            }
        } else {
            file.clear();
            char ignored;
            file.get(ignored);
        }
    }

    if (state.values.empty()) {
        throw std::runtime_error(std::string("quantum random file has no usable numbers: ") + path);
    }

    state.loaded = true;

    if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
        fprintf(stderr, "[quantum-rng] loaded %zu values from %s\n", state.values.size(), path);
    }
}

} // namespace

double llama_quantum_random_01() {
    llama_quantum_random_state & state = llama_quantum_random_get_state();

    std::lock_guard<std::mutex> lock(state.mutex);
    llama_quantum_random_load_locked(state);

    const size_t index_before = state.index;
    const double result = state.values[state.index++];
    if (state.index >= state.values.size()) {
        state.index = 0;
    }

    ++state.total_consumed;

    if (std::getenv("LLAMA_QUANTUM_RNG_LOG")) {
        const bool log_all = std::getenv("LLAMA_QUANTUM_RNG_LOG_ALL") != nullptr;
        if (log_all || state.total_consumed <= 64 || state.index == 0) {
            fprintf(stderr,
                    "[quantum-rng] consumed=%zu index=%zu value=%.17g next_index=%zu%s\n",
                    state.total_consumed, index_before, result, state.index,
                    state.index == 0 ? " wrapped_to_start" : "");
        }
    }

    return result;
}

double llama_quantum_random_at(size_t index) {
    llama_quantum_random_state & state = llama_quantum_random_get_state();

    std::lock_guard<std::mutex> lock(state.mutex);
    llama_quantum_random_load_locked(state);

    return state.values[index % state.values.size()];
}
