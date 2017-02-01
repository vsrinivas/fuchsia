// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/mutex.h>

#include <magenta/syscalls.h>
#include <stdatomic.h>

// TODO(kulakowski) Reintroduce (correctly) optimization counting waiters.

// These values have to be as such. UNLOCKED == 0 allows locks to be
// statically allocated, and the ordering of the values is relied upon
// in the atomic decrement in the unlock routine.
enum {
    UNLOCKED = 0,
    LOCKED = 1,
};

static mx_status_t futex_wait_abstime(mx_futex_t* futex_addr,
                                      int expected_value, mx_time_t abstime) {
    if (abstime == MX_TIME_INFINITE)
        return _mx_futex_wait(futex_addr, expected_value, MX_TIME_INFINITE);

    // TODO(kulakowski) Use MX_CLOCK_UTC when available.
    mx_time_t now = _mx_time_get(MX_CLOCK_MONOTONIC);
    if (abstime < now) {
        return ERR_TIMED_OUT;
    }
    mx_time_t relative_time = abstime - now;
    return _mx_futex_wait(futex_addr, expected_value, relative_time);
}

mx_status_t mxr_mutex_trylock(mxr_mutex_t* mutex) {
    int futex_value = UNLOCKED;
    if (!atomic_compare_exchange_strong(&mutex->futex, &futex_value, LOCKED))
        return ERR_BAD_STATE;
    return NO_ERROR;
}

mx_status_t __mxr_mutex_timedlock(mxr_mutex_t* mutex, mx_time_t abstime) {
    for (;;) {
        switch (atomic_exchange(&mutex->futex, LOCKED)) {
        case UNLOCKED:
            return NO_ERROR;
        case LOCKED: {
            mx_status_t status = futex_wait_abstime(&mutex->futex, LOCKED,
                                                    abstime);
            if (status == ERR_BAD_STATE) {
                continue;
            }
            if (status != NO_ERROR) {
                return status;
            }
            continue;
        }
        }
    }
}

void mxr_mutex_lock(mxr_mutex_t* mutex) {
    mx_status_t status = __mxr_mutex_timedlock(mutex, MX_TIME_INFINITE);
    if (status != NO_ERROR)
        __builtin_trap();
}

void mxr_mutex_unlock(mxr_mutex_t* mutex) {
    atomic_store(&mutex->futex, UNLOCKED);
    mx_status_t status = _mx_futex_wake(&mutex->futex, 0x7FFFFFFF);
    if (status != NO_ERROR)
        __builtin_trap();
}
