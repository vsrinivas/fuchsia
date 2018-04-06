// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/dispatcher.h>
#include <lib/async/receiver.h>

namespace async {

// Holds content for a packet receiver and its handler.
//
// After successfully posting packets to the receiver, the client is responsible
// for retaining it in memory until all packets have been received by the handler
// or the dispatcher has been shutdown.
//
// Multiple packets may be delivered to the same receiver concurrently.
class Receiver final {
public:
    // Handles receipt of packets containing user supplied data.
    //
    // The |status| is |ZX_OK| if the packet was successfully delivered and |data|
    // contains the information from the packet, otherwise |data| is null.
    using Handler = fbl::Function<void(async_t* async,
                                       async::Receiver* receiver,
                                       zx_status_t status,
                                       const zx_packet_user_t* data)>;

    Receiver();
    explicit Receiver(Handler handler);
    ~Receiver();

    Receiver(const Receiver&) = delete;
    Receiver(Receiver&&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver& operator=(Receiver&&) = delete;

    // The handler to invoke when packets are received.
    // Must be set before enqueuing packets.
    const Handler& handler() const { return handler_; }
    void set_handler(Handler handler) { handler_ = static_cast<Handler&&>(handler); }

    // Enqueues a packet of data for delivery to a receiver.
    //
    // The |data| will be copied into the packet.  May be NULL to create a
    // zero-initialized packet payload.
    //
    // Returns |ZX_OK| if the packet was successfully enqueued.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t QueuePacket(async_t* async, const zx_packet_user_t* data = nullptr);

    // Calls |QueuePacket()|.
    // If the result is not |ZX_OK|, synchronously delivers the status to the receiver's handler.
    zx_status_t QueuePacketOrReportError(async_t* async, const zx_packet_user_t* data = nullptr);

private:
    static void CallHandler(async_t* async, async_receiver_t* receiver,
                            zx_status_t status, const zx_packet_user_t* data);

    async_receiver_t receiver_;
    Handler handler_;
};

} // namespace async
