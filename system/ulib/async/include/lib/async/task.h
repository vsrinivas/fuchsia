// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles execution of a posted task.
//
// The |status| is |ZX_OK| if the task's deadline elapsed and the task should run.
// The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
// the task's handler ran or the task was canceled.
typedef void(async_task_handler_t)(async_dispatcher_t* dispatcher,
                                   async_task_t* task,
                                   zx_status_t status);

// Holds context for a task and its handler.
//
// After successfully posting the task, the client is responsible for retaining
// the structure in memory (and unmodified) until the task's handler runs, the task
// is successfully canceled, or the dispatcher shuts down.  Thereafter, the task
// may be posted again or destroyed.
struct async_task {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;

    // The task's handler function.
    async_task_handler_t* handler;

    // The task's deadline must be expressed in the time base used by the asynchronous
    // dispatcher (usually |ZX_CLOCK_MONOTONIC| except in unit tests).
    // See |async_now()| for details.
    zx_time_t deadline;
};

// Posts a task to run on or after its deadline following all posted
// tasks with lesser or equal deadlines.
//
// The task's handler will be invoked exactly once unless the task is canceled.
// When the dispatcher is shutting down (being destroyed), the handlers of
// all remaining tasks will be invoked with a status of |ZX_ERR_CANCELED|.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_post_task(async_dispatcher_t* dispatcher, async_task_t* task);

// Cancels the task associated with |task|.
//
// If successful, the task's handler will not run.
//
// Returns |ZX_OK| if the task was pending and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |ZX_ERR_NOT_FOUND| if there was no pending task either because it
// already ran, had not been posted, or has been dequeued and is pending
// execution (perhaps on another thread).
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_cancel_task(async_dispatcher_t* dispatcher, async_task_t* task);

__END_CDECLS
