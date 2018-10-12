// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_CND_H_
#define LIB_SYNC_CND_H_

#include <lib/sync/mtx.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// A condition variable that works with a sync_mtx_t
typedef struct sync_cnd {
    int lock;
    void* head;
    void* tail;

#ifdef __cplusplus
    sync_cnd()
        : lock(0), head(nullptr), tail(nullptr) {}
#endif
} sync_cnd_t;

#if !defined(__cplusplus)
#define SYNC_CND_INIT ((sync_cnd_t){0})
#endif

// Block until |condvar| is signaled by sync_cnd_signal()/sync_cnd_broadcast(), or a spurious
// wake up occurs.
//
// |mutex| must be in a locked state, and will be atomically unlocked for the duration of the wait,
// then locked again before the function returns.
void sync_cnd_wait(sync_cnd_t* condvar, sync_mtx_t* mutex);

// Block until |condvar| is signaled by sync_cnd_signal()/sync_cnd_broadcast(), or a spurious
// wake up or a timeout occurs.
//
// |mutex| must be in a locked state, and will be atomically unlocked for the duration of the wait,
// then locked again before the function returns.
//
// ZX_TIME_INFINITE can be used for |deadline| to wait for an unlimited amount of time.
//
// Return value:
//      ZX_OK if |condvar| was signaled or a spurious wake up occurred.
//      ZX_ERR_TIMED_OUT if the wait timed out.
zx_status_t sync_cnd_timedwait(sync_cnd_t* condvar, sync_mtx_t* mutex, zx_time_t deadline);

// Wake up one thread waiting for |condvar|
void sync_cnd_signal(sync_cnd_t* condvar);

// Wake up all threads that are currently waiting for |condvar|.
void sync_cnd_broadcast(sync_cnd_t* condvar);

__END_CDECLS

#endif // LIB_SYNC_CND_H_
