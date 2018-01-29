// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>

__BEGIN_CDECLS

// Return codes for |async_task_handler_t|.
typedef enum {
    // The handler has finished the task; it may immediately destroy or
    // reuse the associated task context for another purpose.
    ASYNC_TASK_FINISHED = 0,
    // The handler is requesting for the task to be reiussed upon return;
    // it may modify the task's properties before returning.  In particular,
    // it should modify the task's deadline to prevent the task from
    // immediately retriggering.
    ASYNC_TASK_REPEAT = 1,
} async_task_result_t;

// Handles execution of a posted task.
//
// Reports the |status| of the task.  If the status is |ZX_OK| then the
// task ran, otherwise the task did not run.
//
// The result indicates whether the task should be repeated; it may
// modify the task's properties (such as the deadline) before returning.
//
// The result must be |ASYNC_TASK_FINISHED| if |status| was not |ZX_OK|.
//
// It is safe for the handler to destroy itself when returning |ASYNC_TASK_FINISHED|.
typedef struct async_task async_task_t;
typedef async_task_result_t(async_task_handler_t)(async_t* async,
                                                  async_task_t* task,
                                                  zx_status_t status);

// Context for a posted task.
// A separate instance must be used for each task.
//
// It is customary to aggregate (in C) or subclass (in C++) this structure
// to allow the task context to retain additional state for its handler.
//
// See also |async::Task|.
struct async_task {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke to perform the task.
    async_task_handler_t* handler;
    // The time when the task should run.
    zx_time_t deadline;
    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags;
    // Reserved for future use, set to zero.
    uint32_t reserved;
};

// Posts a task to run on or after its deadline following all posted
// tasks with lesser or equal deadlines.
//
// The client is responsible for allocating and retaining the task context
// until the handler is invoked or until the task is successfully canceled
// using `async_cancel_task()`.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// post new tasks will fail but previously posted tasks can still be canceled
// successfully.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See also |zx_deadline_after()|.
//
// TODO(ZX-976): Strict serial ordering of task dispatch isn't always needed.
// We should consider adding support for multiple independent task queues or
// similar mechanisms.
inline zx_status_t async_post_task(async_t* async, async_task_t* task) {
    return async->ops->post_task(async, task);
}

// Cancels the task associated with |task|.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// post new tasks will fail but previously posted tasks can still be canceled
// successfully.
//
// Returns |ZX_OK| if there was a pending task and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |ZX_ERR_NOT_FOUND| if there was no pending task either because it
// already ran, had not been posted, or has been dequeued and is pending
// execution (perhaps on another thread).
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
inline zx_status_t async_cancel_task(async_t* async, async_task_t* task) {
    return async->ops->cancel_task(async, task);
}

__END_CDECLS
