// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>
#include <async/task.h>
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
    // Reports the |status| of the task.  If the status is |ZX_OK| then the
    // task ran, otherwise the task did not run.
    //
    // The result indicates whether the task should be repeated; it may
    // modify the task's properties (such as the deadline) before returning.
    //
    // The result must be |ASYNC_TASK_FINISHED| if |status| was not |ZX_OK|.
    //
    // It is safe for the handler to destroy itself when returning |ASYNC_TASK_FINISHED|.
    using Handler = fbl::Function<async_task_result_t(async_t* async,
                                                      zx_status_t status)>;

    // Initializes the properties of the task.
    explicit Task(zx_time_t deadline = ZX_TIME_INFINITE, uint32_t flags = 0u);

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
    zx_time_t deadline() const { return async_task_t::deadline; }
    void set_deadline(zx_time_t deadline) { async_task_t::deadline = deadline; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_task_t::flags; }
    void set_flags(uint32_t flags) { async_task_t::flags = flags; }

    // Posts a task to run on or after its deadline following all posted
    // tasks with lesser or equal deadlines.
    //
    // See |async_post_task()| for details.
    zx_status_t Post(async_t* async);

    // Cancels the task.
    //
    // See |async_cancel_task()| for details.
    zx_status_t Cancel(async_t* async);

private:
    static async_task_result_t CallHandler(async_t* async, async_task_t* task,
                                           zx_status_t status);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Task);
};

} // namespace async
