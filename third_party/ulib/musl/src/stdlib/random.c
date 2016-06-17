/*
 * random.c - Copyright © 2011 Szabolcs Nagy
 * Permission to use, copy, modify, and/or distribute this code
 * for any purpose with or without fee is hereby granted.
 * There is no warranty.
*/

#include <stdlib.h>
#include <runtime/compiler.h>
#include <runtime/mutex.h>

struct random_state {
    long counter;
    mxr_mutex_t mutex;
};

static struct random_state r_state = {1, MXR_MUTEX_INIT};

static void __srandom(unsigned seed) {
    r_state.counter = seed;
}

void srandom(unsigned seed) {
    mxr_mutex_lock(&r_state.mutex);
    __srandom(seed);
    mxr_mutex_unlock(&r_state.mutex);
}

// TODO:  ignoring state for now, replace it with secure prng
char* initstate(unsigned seed, char* state, size_t size) {
    mxr_mutex_lock(&r_state.mutex);
    __srandom(seed);
    mxr_mutex_unlock(&r_state.mutex);
    return NULL;
}

// TODO:  ignoring state for now, replace it with secure prng
char* setstate(char* state) {
    return NULL;
}

long random(void) {
    long k;
    mxr_mutex_lock(&r_state.mutex);
    k = r_state.counter;
    if(unlikely(r_state.counter == RAND_MAX)) {
        r_state.counter = 0;
    } else {
        r_state.counter++;
    }
    mxr_mutex_unlock(&r_state.mutex);
    return k;
}
