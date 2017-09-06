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
// Reports the |status| of the task.  If the status is |MX_OK| then the
// task ran, otherwise the task did not run.
//
// The result indicates whether the task should be repeated; it may
// modify the task's properties (such as the deadline) before returning.
//
// The result must be |ASYNC_TASK_FINISHED| if |status| was not |MX_OK|.
//
// It is safe for the handler to destroy itself when returning |ASYNC_TASK_FINISHED|.
typedef struct async_task async_task_t;
typedef async_task_result_t(async_task_handler_t)(async_t* async,
                                                  async_task_t* task,
                                                  mx_status_t status);

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
    mx_time_t deadline;
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
// Returns |MX_OK| if the task was successfully posted.
// Returns |MX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See also |mx_deadline_after()|.
//
// TODO(MG-976): Strict serial ordering of task dispatch isn't always needed.
// We should consider adding support for multiple independent task queues or
// similar mechanisms.
inline mx_status_t async_post_task(async_t* async, async_task_t* task) {
    return async->ops->post_task(async, task);
}

// Cancels the task associated with |task|.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// post new tasks will fail but previously posted tasks can still be canceled
// successfully.
//
// Returns |MX_OK| if there was a pending task and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |MX_ERR_NOT_FOUND| if there was no pending task either because it
// already ran, had not been posted, or has been dequeued and is pending
// execution (perhaps on another thread).
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
inline mx_status_t async_cancel_task(async_t* async, async_task_t* task) {
    return async->ops->cancel_task(async, task);
}

__END_CDECLS

#ifdef __cplusplus

#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a pending task.
//
// This class is thread-safe.
class Task final : private async_task_t {
public:
    // Handles execution of a posted task.
    //
    // Reports the |status| of the task.  If the status is |MX_OK| then the
    // task ran, otherwise the task did not run.
    //
    // The result indicates whether the task should be repeated; it may
    // modify the task's properties (such as the deadline) before returning.
    //
    // The result must be |ASYNC_TASK_FINISHED| if |status| was not |MX_OK|.
    //
    // It is safe for the handler to destroy itself when returning |ASYNC_TASK_FINISHED|.
    using Handler = fbl::Function<async_task_result_t(async_t* async,
                                                      mx_status_t status)>;

    // Initializes the properties of the task.
    explicit Task(mx_time_t deadline = MX_TIME_INFINITE, uint32_t flags = 0u);

    // Destroys the task.
    //
    // This object must not be destroyed until the task has completed, been
    // successfully canceled, or the asynchronous dispatcher itself has been
    // destroyed.
    ~Task();

    // Gets or sets the handler to invoke when the task becomes due.
    // Must be set before posting the task.
    const Handler& handler() const { return handler_; }
    void set_handler(Handler handler) { handler_ = fbl::move(handler); }

    // The time when the task should run.
    mx_time_t deadline() const { return async_task_t::deadline; }
    void set_deadline(mx_time_t deadline) { async_task_t::deadline = deadline; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_task_t::flags; }
    void set_flags(uint32_t flags) { async_task_t::flags = flags; }

    // Posts a task to run on or after its deadline following all posted
    // tasks with lesser or equal deadlines.
    //
    // See |async_post_task()| for details.
    mx_status_t Post(async_t* async);

    // Cancels the task.
    //
    // See |async_cancel_task()| for details.
    mx_status_t Cancel(async_t* async);

private:
    static async_task_result_t CallHandler(async_t* async, async_task_t* task,
                                           mx_status_t status);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Task);
};

} // namespace async

#endif // __cplusplus
