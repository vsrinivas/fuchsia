// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/mutex.h>

#include <zircon/syscalls.h>
#include <stdatomic.h>

// This mutex implementation is based on Ulrich Drepper's paper "Futexes
// Are Tricky" (dated November 5, 2011; see
// http://www.akkadia.org/drepper/futex.pdf).  We use the approach from
// "Mutex, Take 2", with one modification: We use an atomic swap in
// sync_mutex_unlock() rather than an atomic decrement.

// The value of UNLOCKED must be 0 to match C11's mtx.h and so that
// mutexes can be allocated in BSS segments (zero-initialized data).
enum {
    UNLOCKED = 0,
    LOCKED_WITHOUT_WAITERS = 1,
    LOCKED_WITH_WAITERS = 2
};

// On success, this will leave the mutex in the LOCKED_WITH_WAITERS state.
static zx_status_t lock_slow_path(sync_mutex_t* mutex, zx_time_t deadline,
                                  int old_state) {
    for (;;) {
        // If the state shows there are already waiters, or we can update
        // it to indicate that there are waiters, then wait.
        if (old_state == LOCKED_WITH_WAITERS ||
            (old_state == LOCKED_WITHOUT_WAITERS &&
             atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                            LOCKED_WITH_WAITERS))) {
            zx_status_t status = _zx_futex_wait(
                    &mutex->futex, LOCKED_WITH_WAITERS, deadline);
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

zx_status_t sync_mutex_trylock(sync_mutex_t* mutex) {
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITHOUT_WAITERS)) {
        return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
}

zx_status_t sync_mutex_timedlock(sync_mutex_t* mutex, zx_time_t deadline) {
    // Try to claim the mutex.  This compare-and-swap executes the full
    // memory barrier that locking a mutex is required to execute.
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITHOUT_WAITERS)) {
        return ZX_OK;
    }
    return lock_slow_path(mutex, deadline, old_state);
}

void sync_mutex_lock(sync_mutex_t* mutex) __TA_NO_THREAD_SAFETY_ANALYSIS {
    zx_status_t status = sync_mutex_timedlock(mutex, ZX_TIME_INFINITE);
    if (status != ZX_OK) {
        __builtin_trap();
    }
}

void sync_mutex_lock_with_waiter(sync_mutex_t* mutex) __TA_NO_THREAD_SAFETY_ANALYSIS {
    int old_state = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                       LOCKED_WITH_WAITERS)) {
        return;
    }
    zx_status_t status = lock_slow_path(mutex, ZX_TIME_INFINITE, old_state);
    if (status != ZX_OK) {
        __builtin_trap();
    }
}

void sync_mutex_unlock(sync_mutex_t* mutex) __TA_NO_THREAD_SAFETY_ANALYSIS {
    // Attempt to release the mutex.  This atomic swap executes the full
    // memory barrier that unlocking a mutex is required to execute.
    int old_state = atomic_exchange(&mutex->futex, UNLOCKED);

    // At this point, the mutex was unlocked.  In some usage patterns
    // (e.g. for reference counting), another thread might now acquire the
    // mutex and free the memory containing it.  This means we must not
    // dereference |mutex| from this point onwards.

    switch (old_state) {
        case LOCKED_WITHOUT_WAITERS:
            // There were no waiters, so there is nothing more to do.
            break;

        case LOCKED_WITH_WAITERS: {
            // Note that the mutex's memory could have been freed and
            // reused by this point, so this could cause a spurious futex
            // wakeup for a unrelated user of the memory location.
            zx_status_t status = _zx_futex_wake(&mutex->futex, 1);
            if (status != ZX_OK) {
                __builtin_trap();
            }
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
