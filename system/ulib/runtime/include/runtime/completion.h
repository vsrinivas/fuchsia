// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <system/compiler.h>

__BEGIN_CDECLS

typedef struct {
    int futex;
} mxr_completion_t;

#define MXR_COMPLETION_INIT ((mxr_completion_t){0})

#pragma GCC visibility push(hidden)

// Returns ERR_TIMED_OUT if timeout elapses, and NO_ERROR if woken by
// a call to mxr_completion_wake or if the completion has already been
// signalled.
mx_status_t mxr_completion_wait(mxr_completion_t* completion, mx_time_t timeout);

// Awakens all waiters on the completion, and marks the it as
// signaled. Waits after this call but before a reset of the
// completion will also see the signal and immediately return.
void mxr_completion_signal(mxr_completion_t* completion);

// Resets the completion's signalled state to unsignaled.
void mxr_completion_reset(mxr_completion_t* completion);

#pragma GCC visibility pop

__END_CDECLS
