// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>

#include <stdatomic.h>

__BEGIN_CDECLS;

typedef struct {
    atomic_int futex;
} completion_t;

#define COMPLETION_INIT ((completion_t){0})

// Returns MX_ERR_TIMED_OUT if timeout elapses, and MX_OK if woken by
// a call to completion_wake or if the completion has already been
// signaled.
mx_status_t completion_wait(completion_t* completion, mx_time_t timeout);

// Awakens all waiters on the completion, and marks the it as
// signaled. Waits after this call but before a reset of the
// completion will also see the signal and immediately return.
void completion_signal(completion_t* completion);

// Resets the completion's signaled state to unsignaled.
void completion_reset(completion_t* completion);

__END_CDECLS;
