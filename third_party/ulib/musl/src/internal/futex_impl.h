#pragma once

#include "atomic.h"

#include <limits.h>
#include <magenta/syscalls.h>
#include <stdatomic.h>

void __wait(atomic_int* futex, atomic_int* waiters, int current_value);
static inline void __wake(atomic_int* futex, int cnt) {
    if (cnt < 0)
        cnt = INT_MAX;
    _mx_futex_wake(futex, cnt);
}

/* Self-synchronized-destruction-safe lock functions */
#define UNLOCKED 0
#define LOCKED_NO_WAITERS 1
#define LOCKED_MAYBE_WAITERS 2

static inline void lock(atomic_int* l) {
    if (a_cas_shim(l, UNLOCKED, LOCKED_NO_WAITERS)) {
        a_cas_shim(l, LOCKED_NO_WAITERS, LOCKED_MAYBE_WAITERS);
        do
            __wait(l, UNLOCKED, LOCKED_MAYBE_WAITERS);
        while (a_cas_shim(l, UNLOCKED, LOCKED_MAYBE_WAITERS));
    }
}

static inline void unlock(atomic_int* l) {
    if (atomic_exchange(l, UNLOCKED) == LOCKED_MAYBE_WAITERS)
        __wake(l, 1);
}

static inline void unlock_requeue(atomic_int* l, mx_futex_t* r) {
    atomic_store(l, UNLOCKED);
    _mx_futex_requeue(l, /* wake count */ 0, /* l futex value */ UNLOCKED,
                      r, /* requeue count */ 1);
}
