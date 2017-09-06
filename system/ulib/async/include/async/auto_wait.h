// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/wait.h>

#ifdef __cplusplus

#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a pending wait operation which is automatically canceled
// when it goes out of scope.
//
// This class is NOT thread-safe; it can only be used with single-threaded
// asynchronous dispatchers.
class AutoWait final : private async_wait_t {
public:
    // Handles completion of asynchronous wait operations.
    //
    // Reports the |status| of the wait.  If the status is |MX_OK| then |signal|
    // describes the signal which was received, otherwise |signal| is null.
    //
    // The result indicates whether the wait should be repeated; it may
    // modify the wait's properties (such as the trigger) before returning.
    //
    // The result must be |ASYNC_WAIT_FINISHED| if |status| was not |MX_OK|.
    //
    // It is safe for the handler to destroy itself when returning |ASYNC_WAIT_FINISHED|.
    using Handler = fbl::Function<async_wait_result_t(async_t* async,
                                                      mx_status_t status,
                                                      const mx_packet_signal_t* signal)>;

    // Initializes the properties of the wait operation and binds it to an
    // asynchronous dispatcher.
    explicit AutoWait(async_t* async,
                      mx_handle_t object = MX_HANDLE_INVALID,
                      mx_signals_t trigger = MX_SIGNAL_NONE,
                      uint32_t flags = 0u);

    // Destroys the wait operation.
    //
    // The wait is canceled automatically if it is still pending.
    ~AutoWait();

    // Gets the asynchronous dispatcher to which this wait has been bound.
    async_t* async() const { return async_; }

    // Returns true if |Begin()| was called successfully but the wait has not
    // completed or been canceled.
    bool is_pending() const { return pending_; }

    // Gets or sets the handler to invoke when the wait completes.
    // Must be set before beginning the wait.
    const Handler& handler() const { return handler_; }
    void set_handler(Handler handler) { handler_ = fbl::move(handler); }

    // The object to wait for signals on.
    mx_handle_t object() const { return async_wait_t::object; }
    void set_object(mx_handle_t object) { async_wait_t::object = object; }

    // The set of signals to wait for.
    mx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(mx_signals_t trigger) { async_wait_t::trigger = trigger; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals.
    //
    // This method must not be called when the wait is already pending.
    //
    // See |async_begin_wait()| for details.
    mx_status_t Begin();

    // Cancels the wait.
    //
    // This method does nothing if the wait is not pending.
    //
    // See |async_cancel_wait()| for details.
    void Cancel();

private:
    static async_wait_result_t CallHandler(async_t* async, async_wait_t* wait,
                                           mx_status_t status,
                                           const mx_packet_signal_t* signal);

    async_t* const async_;
    Handler handler_;
    bool pending_ = false;

    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoWait);
};

} // namespace async

#endif // __cplusplus
