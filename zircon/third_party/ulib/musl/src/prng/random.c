#define _ALL_SOURCE
#include <stdlib.h>

#include <limits.h>
#include <stdint.h>
#include <threads.h>

// TODO(kulakowski) Implement a real and secure prng

static mtx_t counter_lock = MTX_INIT;
static uint64_t counter;

long random(void) {
    mtx_lock(&counter_lock);
    uint64_t value = ++counter;
    if (counter > RAND_MAX || counter > LONG_MAX)
        counter = 0;
    mtx_unlock(&counter_lock);

    return value;
}

void srandom(unsigned seed) {
    mtx_lock(&counter_lock);
    counter = seed;
    mtx_unlock(&counter_lock);
}

char* initstate(unsigned seed, char* state, size_t n) {
    srandom(seed);
    return (char*)&counter;
}

char* setstate(char* state) {
    return (char*)&counter;
}
