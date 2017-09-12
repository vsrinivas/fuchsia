// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/mutex.h>

#include <zircon/syscalls.h>
#include <stdatomic.h>

// This mutex implementation is based on Ulrich Drepper's paper "Futexes
// Are Tricky" (dated November 5, 2011; see
// http://www.akkadia.org/drepper/futex.pdf).  We use the approach from
// "Mutex, Take 2", with one modification: We use an atomic swap in
// zxr_mutex_unlock() rather than an atomic decrement.

// The value of UNLOCKED must be 0 to match mutex.h and so that mutexes can
// be allocated in BSS segments (zero-initialized data).
enum {
    UNLOCKED = 0,
    LOCKED_WITHOUT_WAITERS = 1,
    LOCKED_WITH_WAITERS = 2
};

// On success, this will leave the mutex in the LOCKED_WITH_WAITERS state.
static zx_status_t lock_slow_path(zxr_mutex_t* mutex, zx_time_t abstime,
                                  int old_state) {
    for (;;) {
        // If the state shows there are already waiters, or we can update
        // it to indicate that there are waiters, then wait.
        if (old_state == LOCKED_WITH_WAITERS ||
            (old_state == LOCKED_WITHOUT_WAITERS &&
             atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                            LOCKED_WITH_WAITERS))) {
            // TODO(kulakowski) Use ZX_CLOCK_UTC when available.
            zx_status_t status = _zx_futex_wait(
                    &mutex->futex, LOCKED_WITH_WAITERS, abstime);
            if (status == ZX_ERR_TIMED_OUT)
                return ZX_ERR_TIMED_OUT;
        }

        // Try again to claim the mutex.  On this try, we must set the
        // mutex state to LOCKED_WITH_WAITERS rather than
        // LOCKED_WITHOUT_WAITERS.  This is because we could have been
        // woken up when many threads are in the wait queue for the mutex.
        old_state = UNLOCKED;
        if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                           LOCKED_WITH_WAITERS)) {
            return ZX_OK;
        }
    }
}

zx_status_t zxr_mutex_trylock(zxr_mutex_t* mutex) {
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITHOUT_WAITERS)) {
        return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
}

zx_status_t __zxr_mutex_timedlock(zxr_mutex_t* mutex, zx_time_t abstime) {
    // Try to claim the mutex.  This compare-and-swap executes the full
    // memory barrier that locking a mutex is required to execute.
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITHOUT_WAITERS)) {
        return ZX_OK;
    }
    return lock_slow_path(mutex, abstime, old_state);
}

void zxr_mutex_lock(zxr_mutex_t* mutex) {
    zx_status_t status = __zxr_mutex_timedlock(mutex, ZX_TIME_INFINITE);
    if (status != ZX_OK)
        __builtin_trap();
}

void zxr_mutex_lock_with_waiter(zxr_mutex_t* mutex) {
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITH_WAITERS)) {
        return;
    }
    zx_status_t status = lock_slow_path(mutex, ZX_TIME_INFINITE, old_state);
    if (status != ZX_OK)
        __builtin_trap();
}

void zxr_mutex_unlock(zxr_mutex_t* mutex) {
    // Attempt to release the mutex.  This atomic swap executes the full
    // memory barrier that unlocking a mutex is required to execute.
    int old_state = atomic_exchange(&mutex->futex, UNLOCKED);
    switch (old_state) {
        case LOCKED_WITHOUT_WAITERS:
            // There were no waiters, so there is nothing more to do.
            break;

        case LOCKED_WITH_WAITERS: {
            zx_status_t status = _zx_futex_wake(&mutex->futex, 1);
            if (status != ZX_OK)
                __builtin_trap();
            break;
        }

        case UNLOCKED:
        default:
            // Either the mutex was unlocked (in which case the unlock call
            // was invalid), or the mutex was in an invalid state.
            __builtin_trap();
            break;
    }
}
