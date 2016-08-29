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

// Attempts to take the lock without blocking. Returns NO_ERROR if the
// lock is obtained, and ERR_BAD_STATE if not.
mx_status_t mxr_mutex_trylock(mxr_mutex_t* mutex);

// Attempts to take the lock before the timeout expires. Returns
// NO_ERROR if the lock is acquired, and ERR_TIMED_OUT if the timeout
// expires.
mx_status_t mxr_mutex_timedlock(mxr_mutex_t* mutex, mx_time_t timeout);

// Blocks until the lock is obtained.
void mxr_mutex_lock(mxr_mutex_t* mutex);

// Unlocks the lock.
void mxr_mutex_unlock(mxr_mutex_t* mutex);

#pragma GCC visibility pop

__END_CDECLS
