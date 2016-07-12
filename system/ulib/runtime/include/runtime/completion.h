// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <magenta/types.h>
#include <system/compiler.h>

__BEGIN_CDECLS

typedef struct {
    int futex;
} mxr_completion_t;

#define MXR_COMPLETION_INIT ((mxr_completion_t){0})

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

__END_CDECLS
