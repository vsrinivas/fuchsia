// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles receipt of packets containing user supplied data.
//
// The |status| is |ZX_OK| if the packet was successfully delivered and |data|
// contains the information from the packet, otherwise |data| is null.
typedef void(async_receiver_handler_t)(async_t* async,
                                       async_receiver_t* receiver,
                                       zx_status_t status,
                                       const zx_packet_user_t* data);

// Holds content for a packet receiver and its handler.
//
// After successfully queuing packets to the receiver, the client is responsible
// for retaining the structure in memory (and unmodified) until all packets have
// been received by the handler or the dispatcher shuts down.  There is no way
// to cancel a packet which has been queued.
//
// Multiple packets may be delivered to the same receiver concurrently.
struct async_receiver {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;

    // The handler to invoke when a packet is received.
    async_receiver_handler_t* handler;
};

// Enqueues a packet of data for delivery to a receiver.
//
// The |data| will be copied into the packet.  May be NULL to create a
// zero-initialized packet payload.
//
// Returns |ZX_OK| if the packet was successfully enqueued.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_queue_packet(async_t* async, async_receiver_t* receiver,
                               const zx_packet_user_t* data);

__END_CDECLS
