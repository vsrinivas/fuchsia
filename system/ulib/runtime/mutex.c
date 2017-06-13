// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/mutex.h>

#include <magenta/syscalls.h>
#include <stdatomic.h>

// This mutex implementation is based on Ulrich Drepper's paper "Futexes
// Are Tricky" (dated November 5, 2011; see
// http://www.akkadia.org/drepper/futex.pdf).  We use the approach from
// "Mutex, Take 2", with one modification: We use an atomic swap in
// mxr_mutex_unlock() rather than an atomic decrement.

// The value of UNLOCKED must be 0 to match mutex.h and so that mutexes can
// be allocated in BSS segments (zero-initialized data).
enum {
    UNLOCKED = 0,
    LOCKED_WITHOUT_WAITERS = 1,
    LOCKED_WITH_WAITERS = 2
};

// On success, this will leave the mutex in the LOCKED_WITH_WAITERS state.
static mx_status_t lock_slow_path(mxr_mutex_t* mutex, mx_time_t abstime,
                                  int old_state) {
    for (;;) {
        // If the state shows there are already waiters, or we can update
        // it to indicate that there are waiters, then wait.
        if (old_state == LOCKED_WITH_WAITERS ||
            (old_state == LOCKED_WITHOUT_WAITERS &&
             atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                            LOCKED_WITH_WAITERS))) {
            // TODO(kulakowski) Use MX_CLOCK_UTC when available.
            mx_status_t status = _mx_futex_wait(
                    &mutex->futex, LOCKED_WITH_WAITERS, abstime);
            if (status == MX_ERR_TIMED_OUT)
                return MX_ERR_TIMED_OUT;
        }

        // Try again to claim the mutex.  On this try, we must set the
        // mutex state to LOCKED_WITH_WAITERS rather than
        // LOCKED_WITHOUT_WAITERS.  This is because we could have been
        // woken up when many threads are in the wait queue for the mutex.
        old_state = UNLOCKED;
        if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                           LOCKED_WITH_WAITERS)) {
            return MX_OK;
        }
    }
}

mx_status_t mxr_mutex_trylock(mxr_mutex_t* mutex) {
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITHOUT_WAITERS)) {
        return MX_OK;
    }
    return MX_ERR_BAD_STATE;
}

mx_status_t __mxr_mutex_timedlock(mxr_mutex_t* mutex, mx_time_t abstime) {
    // Try to claim the mutex.  This compare-and-swap executes the full
    // memory barrier that locking a mutex is required to execute.
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITHOUT_WAITERS)) {
        return MX_OK;
    }
    return lock_slow_path(mutex, abstime, old_state);
}

void mxr_mutex_lock(mxr_mutex_t* mutex) {
    mx_status_t status = __mxr_mutex_timedlock(mutex, MX_TIME_INFINITE);
    if (status != MX_OK)
        __builtin_trap();
}

void mxr_mutex_lock_with_waiter(mxr_mutex_t* mutex) {
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITH_WAITERS)) {
        return;
    }
    mx_status_t status = lock_slow_path(mutex, MX_TIME_INFINITE, old_state);
    if (status != MX_OK)
        __builtin_trap();
}

void mxr_mutex_unlock(mxr_mutex_t* mutex) {
    // Attempt to release the mutex.  This atomic swap executes the full
    // memory barrier that unlocking a mutex is required to execute.
    int old_state = atomic_exchange(&mutex->futex, UNLOCKED);
    switch (old_state) {
        case LOCKED_WITHOUT_WAITERS:
            // There were no waiters, so there is nothing more to do.
            break;

        case LOCKED_WITH_WAITERS: {
            mx_status_t status = _mx_futex_wake(&mutex->futex, 1);
            if (status != MX_OK)
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
