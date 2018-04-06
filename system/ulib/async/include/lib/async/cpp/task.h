// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/task.h>
#include <lib/zx/time.h>

namespace async {

// Posts a task to invoke |handler| with a deadline of now.
//
// The handler will not run if the dispatcher shuts down before it comes due.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
zx_status_t PostTask(async_t* async, fbl::Closure handler);

// Posts a task to invoke |handler| with a deadline expressed as a |delay| from now.
//
// The handler will not run if the dispatcher shuts down before it comes due.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
zx_status_t PostDelayedTask(async_t* async, fbl::Closure handler, zx::duration delay);

// Posts a task to invoke |handler| with the specified |deadline|.
//
// The handler will not run if the dispatcher shuts down before it comes due.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
zx_status_t PostTaskForTime(async_t* async, fbl::Closure handler, zx::time deadline);

// Holds context for a task and its handler.
//
// After successfully posting the task, the client is responsible for retaining
// it in memory until the task's handler runs or the task is successfully canceled.
// Thereafter, the task may be posted again or destroyed.
class Task final {
public:
    // Handles execution of a posted task.
    //
    // The |status| is |ZX_OK| if the task's deadline elapsed and the task should run.
    // The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
    // the task's handler ran or the task was canceled.
    // The |status| may also report errors which occurred during a call to
    // |PostOrReportError()|, |PostDelayedOrReportError()|, or |PostForTimeOrReportError()|.
    using Handler = fbl::Function<void(async_t* async, async::Task* task, zx_status_t status)>;

    Task();
    explicit Task(Handler handler);
    ~Task();

    Task(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&&) = delete;

    // Sets the handler to invoke when the task becomes due.
    void set_handler(Handler handler) { handler_ = static_cast<Handler&&>(handler); }

    // Returns true if the handler has been set.
    bool has_handler() const { return !!handler_; }

    // The last deadline with which the task was posted, or |zx::time::infinite()|
    // if it has never been posted.
    zx::time last_deadline() const { return zx::time(task_.deadline); }

    // Posts a task to invoke the handler with a deadline of now.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Post(async_t* async);

    // Calls |Post()|.
    // If the result is not |ZX_OK|, synchronously delivers the status to the task's handler.
    zx_status_t PostOrReportError(async_t* async);

    // Posts a task to invoke the handler with a deadline expressed as a |delay| from now.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t PostDelayed(async_t* async, zx::duration delay);

    // Calls |PostDelayed()|.
    // If the result is not |ZX_OK|, synchronously delivers the status to the task's handler.
    zx_status_t PostDelayedOrReportError(async_t* async, zx::duration delay);

    // Posts a task to invoke the handler with the specified |deadline|.
    //
    // The |deadline| must be expressed in the time base used by the asynchronous
    // dispatcher (usually |ZX_CLOCK_MONOTONIC| except in unit tests).
    // See |async_now()| for details.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t PostForTime(async_t* async, zx::time deadline);

    // Calls |PostForTime()|.
    // If the result is not |ZX_OK|, synchronously delivers the status to the task's handler.
    zx_status_t PostForTimeOrReportError(async_t* async, zx::time deadline);

    // Cancels the task associated with |task|.
    //
    // If successful, the task's handler will not run.
    //
    // Returns |ZX_OK| if the task was pending and it has been successfully
    // canceled; its handler will not run again and can be released immediately.
    // Returns |ZX_ERR_NOT_FOUND| if task was not pending either because its
    // handler already ran, the task had not been posted, or the task has
    // already been dequeued and is pending execution (perhaps on another thread).
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Cancel(async_t* async);

private:
    static void CallHandler(async_t* async, async_task_t* task, zx_status_t status);

    async_task_t task_;
    Handler handler_;
};

// Holds context for a task and its handler, with RAII semantics.
// Automatically cancels the task when it goes out of scope.
//
// After successfully posting the task, the client is responsible for retaining
// it in memory until the task's handler runs or the task is successfully canceled.
// Thereafter, the task may be posted again or destroyed.
//
// This class must only be used with single-threaded asynchronous dispatchers
// and must only be accessed on the dispatch thread since it lacks internal
// synchronization of its state.
class AutoTask final {
public:
    // Handles execution of a posted task.
    //
    // The |status| is |ZX_OK| if the task's deadline elapsed and the task should run.
    // The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
    // the task's handler ran or the task was canceled.
    // The |status| may also report errors which occurred during a call to
    // |PostOrReportError()|, |PostDelayedOrReportError()|, or |PostForTimeOrReportError()|.
    using Handler = fbl::Function<void(async_t* async, async::AutoTask* task, zx_status_t status)>;

    AutoTask();
    explicit AutoTask(Handler handler);
    ~AutoTask();

    AutoTask(const AutoTask&) = delete;
    AutoTask(AutoTask&&) = delete;
    AutoTask& operator=(const AutoTask&) = delete;
    AutoTask& operator=(AutoTask&&) = delete;

    // Sets the handler to invoke when the task becomes due.
    void set_handler(Handler handler) { handler_ = static_cast<Handler&&>(handler); }

    // Returns true if the handler has been set.
    bool has_handler() const { return !!handler_; }

    // Returns true if the task has been posted and has not yet executed or been canceled.
    bool is_pending() const { return async_ != nullptr; }

    // The last deadline with which the task was posted, or |zx::time::infinite()|
    // if it has never been posted.
    zx::time last_deadline() const { return zx::time(task_.deadline); }

    // Posts a task to invoke the handler with a deadline of now.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down or if the
    // task is already pending.
    // Returns |ZX_ERR_ALREADY_EXISTS| if the task is already pending.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Post(async_t* async);

    // Calls |Post()|.
    // If the result is not |ZX_OK| and the task was not already pending,
    // synchronously delivers the status to the task's handler.
    zx_status_t PostOrReportError(async_t* async);

    // Posts a task to invoke the handler with a deadline expressed as a |delay| from now.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down or if the
    // task is already pending.
    // Returns |ZX_ERR_ALREADY_EXISTS| if the task is already pending.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t PostDelayed(async_t* async, zx::duration delay);

    // Calls |PostDelayed()|.
    // If the result is not |ZX_OK| and the task was not already pending,
    // synchronously delivers the status to the task's handler.
    zx_status_t PostDelayedOrReportError(async_t* async, zx::duration delay);

    // Posts a task to invoke the handler with the specified |deadline|.
    //
    // The |deadline| must be expressed in the time base used by the asynchronous
    // dispatcher (usually |ZX_CLOCK_MONOTONIC| except in unit tests).
    // See |async_now()| for details.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down or if the
    // task is already pending.
    // Returns |ZX_ERR_ALREADY_EXISTS| if the task is already pending.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t PostForTime(async_t* async, zx::time deadline);

    // Calls |PostForTime()|.
    // If the result is not |ZX_OK| and the task was not already pending,
    // synchronously delivers the status to the task's handler.
    zx_status_t PostForTimeOrReportError(async_t* async, zx::time deadline);

    // Cancels the task associated with |task|.
    //
    // If successful, the task's handler will not run.
    //
    // Returns |ZX_OK| if the task was pending and it has been successfully
    // canceled; its handler will not run again and can be released immediately.
    // Returns |ZX_ERR_NOT_FOUND| if task was not pending either because its
    // handler already ran, the task had not been posted, or the task has
    // already been dequeued and is pending execution (perhaps on another thread).
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Cancel();

private:
    static void CallHandler(async_t* async, async_task_t* task, zx_status_t status);

    async_task_t task_;
    Handler handler_;
    async_t* async_ = nullptr;
};

} // namespace async
