// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/task.h>

#ifdef __cplusplus

#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a pending task which is automatically canceled when
// it goes out of scope.
//
// This class is NOT thread-safe; it can only be used with single-threaded
// asynchronous dispatchers.
class AutoTask final : private async_task_t {
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

    // Initializes the properties of the task and binds it to an asynchronous
    // dispatcher.
    explicit AutoTask(async_t* async,
                      mx_time_t deadline = MX_TIME_INFINITE,
                      uint32_t flags = 0u);

    // Destroys the task.
    //
    // The task is canceled automatically if it is still pending.
    ~AutoTask();

    // Gets the asynchronous dispatcher to which this task has been bound.
    async_t* async() const { return async_; }

    // Returns true if the |Post()| was called successfully but the task has
    // not started execution or been canceled.
    bool is_pending() const { return pending_; }

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
    mx_status_t Post();

    // Cancels the task.
    //
    // This method does nothing if the wait is not pending.
    //
    // See |async_cancel_task()| for details.
    void Cancel();

private:
    static async_task_result_t CallHandler(async_t* async, async_task_t* task,
                                           mx_status_t status);

    async_t* const async_;
    Handler handler_;
    bool pending_ = false;

    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoTask);
};

} // namespace async

#endif // __cplusplus
