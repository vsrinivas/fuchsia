// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/task.h>
#include <async/wait.h>

#ifdef __cplusplus

#include <mxtl/function.h>
#include <mxtl/macros.h>

namespace async {

// C++ wrapper for a pending wait operation with an associated timeout.
//
// Use |MX_TIME_INFINITE| as the deadline to wait indefinitely.
//
// Warning: This helper will only work correctly with non-concurrent dispatchers.
//
// Implementation note: The task's flags are managed internally by this object
// so they are not exposed to the client unlike the wait flags.
class WaitWithTimeout final : private async_wait_t, private async_task_t {
public:
    // Handles completion of asynchronous wait operations or a timeout.
    //
    // Reports the |status| of the wait.  If the status is |MX_OK| then |signal|
    // describes the signal which was received, otherwise |signal| is null.
    //
    // Timeouts are indicated with status |MX_ERR_TIMED_OUT|.
    //
    // The result indicates whether the wait should be repeated; it may
    // modify the wait's properties (such as the trigger) before returning.
    //
    // The result must be |ASYNC_WAIT_FINISHED| if |status| was not |MX_OK|.
    //
    // It is safe for the handler to destroy itself when returning |ASYNC_WAIT_FINISHED|.
    using Handler = mxtl::Function<async_wait_result_t(async_t* async,
                                                       mx_status_t status,
                                                       const mx_packet_signal_t* signal)>;

    // Initializes the properties of the wait with timeout operation.
    explicit WaitWithTimeout(mx_handle_t object = MX_HANDLE_INVALID,
                             mx_signals_t trigger = MX_SIGNAL_NONE,
                             mx_time_t deadline = MX_TIME_INFINITE,
                             uint32_t flags = 0u);

    // Destroys the wait with timeout operation.
    //
    // This object must not be destroyed until the wait has completed, been
    // successfully canceled, timed out, or the asynchronous dispatcher itself
    // has been destroyed.
    ~WaitWithTimeout();

    // Gets or sets the handler to invoke when the wait completes.
    // Must be set before beginning the wait.
    const Handler& handler() const { return handler_; }
    void set_handler(Handler handler) { handler_ = mxtl::move(handler); }

    // The object to wait for signals on.
    mx_handle_t object() const { return async_wait_t::object; }
    void set_object(mx_handle_t object) { async_wait_t::object = object; }

    // The set of signals to wait for.
    mx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(mx_signals_t trigger) { async_wait_t::trigger = trigger; }

    // The time when the timeout should occur.
    mx_time_t deadline() const { return async_task_t::deadline; }
    void set_deadline(mx_time_t deadline) { async_task_t::deadline = deadline; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals or for the timeout deadline to elapse.
    //
    // See |async_begin_wait()| for details.
    mx_status_t Begin(async_t* async);

    // Cancels the wait and its associated timeout.
    //
    // See |async_cancel_wait()| for details.
    mx_status_t Cancel(async_t* async);

private:
    static async_wait_result_t WaitHandler(async_t* async, async_wait_t* wait,
                                           mx_status_t status,
                                           const mx_packet_signal_t* signal);
    static async_task_result_t TimeoutHandler(async_t* async, async_task_t* task,
                                              mx_status_t status);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(WaitWithTimeout);
};

} // namespace async

#endif // __cplusplus
