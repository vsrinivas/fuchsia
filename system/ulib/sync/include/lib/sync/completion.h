// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_COMPLETION_H_
#define LIB_SYNC_COMPLETION_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct sync_completion {
    zx_futex_t futex;

#ifdef __cplusplus
    sync_completion()
        : futex(0) {}
#endif
} sync_completion_t;

#if !defined(__cplusplus)
#define SYNC_COMPLETION_INIT ((sync_completion_t){0})
#endif

// Returns ZX_ERR_TIMED_OUT if timeout elapses, and ZX_OK if woken by
// a call to sync_completion_signal or if the completion has already been
// signaled.
zx_status_t sync_completion_wait(sync_completion_t* completion, zx_duration_t timeout);

// Returns ZX_ERR_TIMED_OUT if deadline elapses, and ZX_OK if woken by
// a call to sync_completion_signal or if the completion has already been
// signaled.
zx_status_t sync_completion_wait_deadline(sync_completion_t* completion, zx_time_t deadline);

// Awakens all waiters on the completion, and marks it as
// signaled. Waits after this call but before a reset of the
// completion will also see the signal and immediately return.
void sync_completion_signal(sync_completion_t* completion);

// Marks the completion as signaled, but doesn't awaken all waiters
// right away. Instead, all waiters are requeued to the |futex|.
// Waits after this call but before a reset of the
// completion will also see the signal and immediately return.
//
// Intended to be used by libsync internally, e.g. the condition variable
// implementation.
void sync_completion_signal_requeue(sync_completion_t* completion, zx_futex_t* futex);

// Resets the completion's signaled state to unsignaled.
void sync_completion_reset(sync_completion_t* completion);

__END_CDECLS

#endif // LIB_SYNC_COMPLETION_H_
