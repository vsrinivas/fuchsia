// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles completion of asynchronous wait operations.
//
// The |status| is |ZX_OK| if the wait was satisfied and |signal| is non-null.
// The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
// the task's handler ran or the task was canceled.
typedef void(async_wait_handler_t)(async_t* async,
                                   async_wait_t* wait,
                                   zx_status_t status,
                                   const zx_packet_signal_t* signal);

// Holds context for an asynchronous wait operation and its handler.
//
// After successfully beginning the wait, the client is responsible for retaining
// the structure in memory (and unmodified) until the wait's handler runs, the wait
// is successfully canceled, or the dispatcher shuts down.  Thereafter, the wait
// may be started begun or destroyed.
struct async_wait {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;

    // The wait's handler function.
    async_wait_handler_t* handler;

    // The object to wait for signals on.
    zx_handle_t object;

    // The set of signals to wait for.
    zx_signals_t trigger;
};

// Begins asynchronously waiting for an object to receive one or more signals
// specified in |wait|.  Invokes the handler when the wait completes.
//
// The wait's handler will be invoked exactly once unless the wait is canceled.
// When the dispatcher is shutting down (being destroyed), the handlers of
// all remaining waits will be invoked with a status of |ZX_ERR_CANCELED|.
//
// Returns |ZX_OK| if the wait was successfully begun.
// Returns |ZX_ERR_ACCESS_DENIED| if the object does not have |ZX_RIGHT_WAIT|.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_begin_wait(async_t* async, async_wait_t* wait);

// Cancels the wait associated with |wait|.
//
// If successful, the wait's handler will not run.
//
// Returns |ZX_OK| if the wait was pending and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |ZX_ERR_NOT_FOUND| if there was no pending wait either because it
// already completed, had not been started, or its completion packet has been
// dequeued from the port and is pending delivery to its handler (perhaps on
// another thread).
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_cancel_wait(async_t* async, async_wait_t* wait);

__END_CDECLS
