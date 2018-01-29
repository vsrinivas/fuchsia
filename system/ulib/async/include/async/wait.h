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
// Reports the |status| of the wait.  If the status is |ZX_OK| then |signal|
// describes the signal which was received, otherwise |signal| is null.
//
// The result indicates whether the wait should be repeated; it may
// modify the wait's properties (such as the trigger) before returning.
//
// The result must be |ASYNC_WAIT_FINISHED| if |status| was not |ZX_OK|.
//
// It is safe for the handler to destroy itself when returning |ASYNC_WAIT_FINISHED|.
typedef async_wait_result_t(async_wait_handler_t)(async_t* async,
                                                  async_wait_t* wait,
                                                  zx_status_t status,
                                                  const zx_packet_signal_t* signal);

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
    zx_handle_t object;
    // The set of signals to wait for.
    zx_signals_t trigger;
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
// Returns |ZX_OK| if the wait has been successfully started.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |zx_object_wait_async()|.
inline zx_status_t async_begin_wait(async_t* async, async_wait_t* wait) {
    return async->ops->begin_wait(async, wait);
}

// Cancels the wait associated with |wait|.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// begin new waits will fail but previously begun waits can still be canceled
// successfully.
//
// Returns |ZX_OK| if there was a pending wait and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |ZX_ERR_NOT_FOUND| if there was no pending wait either because it
// already completed, had not been started, or its completion packet has been
// dequeued and is pending delivery to its handler (perhaps on another thread).
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |zx_port_cancel()|.
inline zx_status_t async_cancel_wait(async_t* async, async_wait_t* wait) {
    return async->ops->cancel_wait(async, wait);
}

__END_CDECLS
