// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>

__BEGIN_CDECLS

// Return codes for |async_wait_handler_t|.
typedef enum {
    // The handler has finished waiting; it may immediately destroy or
    // reuse the associated wait context for another purpose.
    ASYNC_WAIT_FINISHED = 0,
    // The handler is requesting for the wait to be reiussed upon return;
    // it may modify the wait's properties before returning.
    ASYNC_WAIT_AGAIN = 1,
} async_wait_result_t;

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
typedef async_wait_result_t(async_wait_handler_t)(async_t* async,
                                                  async_wait_t* wait,
                                                  mx_status_t status,
                                                  const mx_packet_signal_t* signal);

// Context for an asynchronous wait operation.
// A separate instance must be used for each wait.
//
// It is customary to aggregate (in C) or subclass (in C++) this structure
// to allow the wait context to retain additional state for its handler.
//
// See also |async::Task|.
typedef struct async_wait async_wait_t;
struct async_wait {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke on completion of the wait.
    async_wait_handler_t* handler;
    // The object to wait for signals on.
    mx_handle_t object;
    // The set of signals to wait for.
    mx_signals_t trigger;
    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags;
    // Reserved for future use, set to zero.
    uint32_t reserved;
};

// Begins asynchronously waiting for an object to receive one or more signals
// specified in |wait|.  Invokes the handler when the wait completes.
//
// The client is responsible for allocating and retaining the wait context
// until the handler runs or the wait is successfully canceled using
// `async_cancel_wait()`.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// begin new waits will fail but previously begun waits can still be canceled
// successfully.
//
// Returns |MX_OK| if the wait has been successfully started.
// Returns |MX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |mx_object_wait_async()|.
inline mx_status_t async_begin_wait(async_t* async, async_wait_t* wait) {
    return async->ops->begin_wait(async, wait);
}

// Cancels the wait associated with |wait|.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// begin new waits will fail but previously begun waits can still be canceled
// successfully.
//
// Returns |MX_OK| if there was a pending wait and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |MX_ERR_NOT_FOUND| if there was no pending wait either because it
// already completed, had not been started, or its completion packet has been
// dequeued and is pending delivery to its handler (perhaps on another thread).
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |mx_port_cancel()|.
inline mx_status_t async_cancel_wait(async_t* async, async_wait_t* wait) {
    return async->ops->cancel_wait(async, wait);
}

__END_CDECLS

#ifdef __cplusplus

#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a pending wait operation.
//
// This class is thread-safe.
class Wait final : private async_wait_t {
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

    // Initializes the properties of the wait operation.
    explicit Wait(mx_handle_t object = MX_HANDLE_INVALID,
                  mx_signals_t trigger = MX_SIGNAL_NONE, uint32_t flags = 0u);

    // Destroys the wait operation.
    //
    // This object must not be destroyed until the wait has completed, been
    // successfully canceled, or the asynchronous dispatcher itself has
    // been destroyed.
    ~Wait();

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
    // See |async_begin_wait()| for details.
    mx_status_t Begin(async_t* async);

    // Cancels the wait.
    //
    // See |async_cancel_wait()| for details.
    mx_status_t Cancel(async_t* async);

private:
    static async_wait_result_t CallHandler(async_t* async, async_wait_t* wait,
                                           mx_status_t status, const mx_packet_signal_t* signal);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Wait);
};

} // namespace async

#endif // __cplusplus
