// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_MUTEX_H_
#define LIB_SYNC_MUTEX_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// An optimal, non-recursive mutex on Fuchsia.
//
// The |mutex_t| mutex in the standard library has several quirks in its design
// that prevent it from being optimal. For example, the |mutex_t| interface
// supports recursion, which adds a branch to |mutex_init| to check that the
// client has not asked for recusion, and |mutex_timedlock| operates in
// |struct timespec| rather than |zx_time_t|.
//
// |sync_mutex| resolves these issues.
typedef struct __TA_CAPABILITY("mutex") sync_mutex {
    zx_futex_t futex;

#ifdef __cplusplus
    sync_mutex()
        : futex(0) {}
#endif
} sync_mutex_t;

#if !defined(__cplusplus)
#define SYNC_MUTEX_INIT ((sync_mutex_t){0})
#endif

// Locks the mutex.
//
// The current thread will block until the mutex is acquired. The mutex is
// non-recursive, which means attempting to lock a mutex that is already held by
// this thread will deadlock.
void sync_mutex_lock(sync_mutex_t* mutex) __TA_ACQUIRE(mutex);

// Locks the mutex and mark the mutex as having a waiter.
//
// Similar to |sync_mutex_lock| but markes the mutex as having a waiter. Intended
// to be used by the condition variable implementation.
void sync_mutex_lock_with_waiter(sync_mutex_t* mutex) __TA_ACQUIRE(mutex);

// Attempt to lock the mutex until |deadline|.
//
// The current thread will block until either the mutex is acquired or
// |deadline| passes.
//
// |deadline| is expressed as an absolute time in the ZX_CLOCK_MONOTONIC
// timebase.
//
// Returns |ZX_OK| if the lock is acquired, and |ZX_ERR_TIMED_OUT| if the
// deadline passes.
zx_status_t sync_mutex_timedlock(sync_mutex_t* mutex, zx_time_t deadline);

// Attempts to lock the mutex without blocking.
//
// Returns |ZX_OK| if the lock is obtained, and |ZX_ERR_BAD_STATE| if not.
zx_status_t sync_mutex_trylock(sync_mutex_t* mutex);

// Unlocks the mutex.
//
// Does nothing if the mutex is already unlocked.
void sync_mutex_unlock(sync_mutex_t* mutex) __TA_RELEASE(mutex);

__END_CDECLS

#endif // LIB_SYNC_MUTEX_H_
