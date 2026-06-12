#pragma once

#include <cstddef>

double llama_quantum_random_01();
double llama_quantum_random_next_01();
const char * llama_quantum_random_last_text();
void llama_quantum_qrng_shutdown();
