#include <stdlib.h>

#include <limits.h>
#include <runtime/mutex.h>

// TODO(kulakowski) Implement a real and secure prng

static mxr_mutex_t counter_lock;
static uint64_t counter;

long random(void) {
    mxr_mutex_lock(&counter_lock);
    uint64_t value = ++counter;
    if (counter > RAND_MAX || counter > LONG_MAX)
        counter = 0;
    mxr_mutex_unlock(&counter_lock);

    return value;
}

void srandom(unsigned seed) {
    mxr_mutex_lock(&counter_lock);
    counter = seed;
    mxr_mutex_unlock(&counter_lock);
}

char* initstate(unsigned seed, char* state, size_t n) {
    srandom(seed);
    return (char*)&counter;
}

char* setstate(char* state) {
    return (char*)&counter;
}
