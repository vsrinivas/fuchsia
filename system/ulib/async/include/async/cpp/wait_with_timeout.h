// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/task.h>
#include <async/wait.h>
#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a pending wait operation with an associated timeout.
//
// Use |ZX_TIME_INFINITE| as the deadline to wait indefinitely.
//
// This class is NOT thread-safe; it can only be used with single-threaded
// asynchronous dispatchers.
//
// Implementation note: The task's flags are managed internally by this object
// so they are not exposed to the client unlike the wait flags.
class WaitWithTimeout final : private async_wait_t, private async_task_t {
public:
    // Handles completion of asynchronous wait operations or a timeout.
    //
    // Reports the |status| of the wait.  If the status is |ZX_OK| then |signal|
    // describes the signal which was received, otherwise |signal| is null.
    //
    // Timeouts are indicated with status |ZX_ERR_TIMED_OUT|.
    //
    // The result indicates whether the wait should be repeated; it may
    // modify the wait's properties (such as the trigger) before returning.
    //
    // The result must be |ASYNC_WAIT_FINISHED| if |status| was not |ZX_OK|.
    //
    // It is safe for the handler to destroy itself when returning |ASYNC_WAIT_FINISHED|.
    using Handler = fbl::Function<async_wait_result_t(async_t* async,
                                                      zx_status_t status,
                                                      const zx_packet_signal_t* signal)>;

    // Initializes the properties of the wait with timeout operation.
    explicit WaitWithTimeout(zx_handle_t object = ZX_HANDLE_INVALID,
                             zx_signals_t trigger = ZX_SIGNAL_NONE,
                             zx_time_t deadline = ZX_TIME_INFINITE,
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
    void set_handler(Handler handler) { handler_ = fbl::move(handler); }

    // The object to wait for signals on.
    zx_handle_t object() const { return async_wait_t::object; }
    void set_object(zx_handle_t object) { async_wait_t::object = object; }

    // The set of signals to wait for.
    zx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(zx_signals_t trigger) { async_wait_t::trigger = trigger; }

    // The time when the timeout should occur.
    zx_time_t deadline() const { return async_task_t::deadline; }
    void set_deadline(zx_time_t deadline) { async_task_t::deadline = deadline; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals or for the timeout deadline to elapse.
    //
    // See |async_begin_wait()| for details.
    zx_status_t Begin(async_t* async);

    // Cancels the wait and its associated timeout.
    //
    // See |async_cancel_wait()| for details.
    zx_status_t Cancel(async_t* async);

private:
    static async_wait_result_t WaitHandler(async_t* async, async_wait_t* wait,
                                           zx_status_t status,
                                           const zx_packet_signal_t* signal);
    static async_task_result_t TimeoutHandler(async_t* async, async_task_t* task,
                                              zx_status_t status);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(WaitWithTimeout);
};

} // namespace async
