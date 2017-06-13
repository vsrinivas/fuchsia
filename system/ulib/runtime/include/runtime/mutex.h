// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <stdatomic.h>

__BEGIN_CDECLS

typedef struct {
    atomic_int futex;
} mxr_mutex_t;

#define MXR_MUTEX_INIT ((mxr_mutex_t){})

#pragma GCC visibility push(hidden)

// Attempts to take the lock without blocking. Returns MX_OK if the
// lock is obtained, and MX_ERR_BAD_STATE if not.
mx_status_t mxr_mutex_trylock(mxr_mutex_t* mutex);

// Attempts to take the lock before the timeout expires. This takes an
// absolute time. Returns MX_OK if the lock is acquired, and
// MX_ERR_TIMED_OUT if the timeout expires.
//
// This function is only for use by mtx_timedlock().
mx_status_t __mxr_mutex_timedlock(mxr_mutex_t* mutex, mx_time_t abstime);

// Blocks until the lock is obtained.
void mxr_mutex_lock(mxr_mutex_t* mutex);

// Unlocks the lock.
void mxr_mutex_unlock(mxr_mutex_t* mutex);

// This is the same as mxr_mutex_lock() except that it always marks the
// mutex as having a waiter.  This is intended for use by condvar
// implementations.  This means that a thread waiting on a condvar futex
// can be requeued onto the mutex's futex, so that a later call to
// mxr_mutex_unlock() will wake that thread.
void mxr_mutex_lock_with_waiter(mxr_mutex_t* mutex);

#pragma GCC visibility pop

__END_CDECLS
