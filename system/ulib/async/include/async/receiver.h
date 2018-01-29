// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>

__BEGIN_CDECLS

// Receives packets containing user supplied data.
//
// Reports the |status| of the receiver.  If the status is |ZX_OK| then |data|
// describes the contents of the packet which was received, otherwise |data|
// is null.
//
// It is safe for the handler to destroy itself when there are no remaining
// packets pending delivery to it.
typedef struct async_receiver async_receiver_t;
typedef void(async_receiver_handler_t)(async_t* async,
                                       async_receiver_t* receiver,
                                       zx_status_t status,
                                       const zx_packet_user_t* data);

// Context for packet receiver.
// The same instance may be used to receive arbitrarily many queued packets.
//
// It is customary to aggregate (in C) or subclass (in C++) this structure
// to allow the receiver context to retain additional state for its handler.
//
// See also |async::Receiver|.
struct async_receiver {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke when a packet is received.
    async_receiver_handler_t* handler;
    // Valid flags: None, set to zero.
    uint32_t flags;
    // Reserved for future use, set to zero.
    uint32_t reserved;
};

// Enqueues a packet of data for delivery to a receiver.
//
// The client is responsible for allocating and retaining the packet context
// until all packets have been delivered.
//
// The |data| will be copied into the packet.  May be NULL to create a
// zero-initialized packet payload.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// queue new packets will fail.
//
// Returns |ZX_OK| if the packet was successfully enqueued.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |zx_port_queue()|.
inline zx_status_t async_queue_packet(async_t* async, async_receiver_t* receiver,
                                      const zx_packet_user_t* data) {
    return async->ops->queue_packet(async, receiver, data);
}

__END_CDECLS
