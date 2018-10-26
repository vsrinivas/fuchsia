// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_CONDITION_H_
#define LIB_SYNC_CONDITION_H_

#include <lib/sync/mutex.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// A condition variable that works with a sync_mutex_t
typedef struct sync_condition {
    int lock;
    void* head;
    void* tail;

#ifdef __cplusplus
    sync_condition()
        : lock(0), head(nullptr), tail(nullptr) {}
#endif
} sync_condition_t;

#if !defined(__cplusplus)
#define SYNC_CONDITION_INIT ((sync_condition_t){0})
#endif

// Block until |condition| is signaled by sync_condition_signal()/sync_condition_broadcast(), or a spurious
// wake up occurs.
//
// |mutex| must be in a locked state, and will be atomically unlocked for the duration of the wait,
// then locked again before the function returns.
void sync_condition_wait(sync_condition_t* condition, sync_mutex_t* mutex);

// Block until |condition| is signaled by sync_condition_signal()/sync_condition_broadcast(), or a spurious
// wake up or a timeout occurs.
//
// |mutex| must be in a locked state, and will be atomically unlocked for the duration of the wait,
// then locked again before the function returns.
//
// ZX_TIME_INFINITE can be used for |deadline| to wait for an unlimited amount of time.
//
// Return value:
//      ZX_OK if |condition| was signaled or a spurious wake up occurred.
//      ZX_ERR_TIMED_OUT if the wait timed out.
zx_status_t sync_condition_timedwait(sync_condition_t* condition, sync_mutex_t* mutex, zx_time_t deadline);

// Wake up one thread waiting for |condition|
void sync_condition_signal(sync_condition_t* condition);

// Wake up all threads that are currently waiting for |condition|.
void sync_condition_broadcast(sync_condition_t* condition);

__END_CDECLS

#endif // LIB_SYNC_CONDITION_H_
