// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <stdatomic.h>

__BEGIN_CDECLS

typedef struct {
    atomic_int futex;
} zxr_mutex_t;

#define ZXR_MUTEX_INIT ((zxr_mutex_t){})

#pragma GCC visibility push(hidden)

// Attempts to take the lock without blocking. Returns ZX_OK if the
// lock is obtained, and ZX_ERR_BAD_STATE if not.
zx_status_t zxr_mutex_trylock(zxr_mutex_t* mutex);

// Attempts to take the lock before the timeout expires. This takes an
// absolute time. Returns ZX_OK if the lock is acquired, and
// ZX_ERR_TIMED_OUT if the timeout expires.
//
// This function is only for use by mtx_timedlock().
zx_status_t __zxr_mutex_timedlock(zxr_mutex_t* mutex, zx_time_t abstime);

// Blocks until the lock is obtained.
void zxr_mutex_lock(zxr_mutex_t* mutex);

// Unlocks the lock.
void zxr_mutex_unlock(zxr_mutex_t* mutex);

// This is the same as zxr_mutex_lock() except that it always marks the
// mutex as having a waiter.  This is intended for use by condvar
// implementations.  This means that a thread waiting on a condvar futex
// can be requeued onto the mutex's futex, so that a later call to
// zxr_mutex_unlock() will wake that thread.
void zxr_mutex_lock_with_waiter(zxr_mutex_t* mutex);

#pragma GCC visibility pop

__END_CDECLS
