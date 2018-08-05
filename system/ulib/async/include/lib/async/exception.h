// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_EXCEPTION_H_
#define LIB_ASYNC_EXCEPTION_H_

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles receipt of packets containing exception reports.
//
// The |status| is |ZX_OK| if the packet was successfully delivered and |data|
// contains the information from the packet, otherwise |data| is null.
// The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down.
typedef void(async_exception_handler_t)(async_dispatcher_t* dispatcher,
                                        async_exception_t* exception,
                                        zx_status_t status,
                                        const zx_port_packet_t* report);

// Holds content for an exception packet receiver and its handler.
//
// The client is responsible for retaining the structure in memory
// (and unmodified) until all packets have been received by the handler or the
// dispatcher shuts down.
//
// Multiple packets may be delivered to the same receiver concurrently.
struct async_exception {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;

    // The handler to invoke when a packet is received.
    async_exception_handler_t* handler;

    // The task we're watching.
    zx_handle_t task;

    // The options to pass to zx_task_bind_exception_port().
    uint32_t options;
};

// Bind the async port to the task's exception port.
//
// Returns |ZX_OK| if task's exception port was successfully bound to.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
// Other error values are possible. See the documentation for
// |zx_task_bind_exception_port()|.
//
// This operation is thread-safe.
zx_status_t async_bind_exception_port(async_dispatcher_t* dispatcher,
                                      async_exception_t* exception);

// Unbind the async port from |task|'s exception port.
//
// Returns |ZX_OK| if the task's exception port was successfully unbound.
// Returns |ZX_ERR_NOT_FOUND| if the port is not bound.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
// Other error values are possible. See the documentation for
// |zx_task_bind_exception_port()|.
//
// This operation is thread-safe.
zx_status_t async_unbind_exception_port(async_dispatcher_t* dispatcher,
                                        async_exception_t* exception);

__END_CDECLS

#endif  // LIB_ASYNC_EXCEPTION_H_
