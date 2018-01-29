// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>
#include <async/receiver.h>
#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a packet receiver.
// The same instance may be used to receive arbitrarily many queued packets.
//
// This class is thread-safe.
class Receiver final : private async_receiver_t {
public:
    // Receives packets containing user supplied data.
    //
    // Reports the |status| of the receiver.  If the status is |ZX_OK| then |data|
    // describes the contents of the packet which was received, otherwise |data|
    // is null.
    //
    // It is safe for the handler to destroy itself when there are no remaining
    // packets pending delivery to it.
    using Handler = fbl::Function<void(async_t* async,
                                       zx_status_t status,
                                       const zx_packet_user_t* data)>;

    // Initializes the properties of the receiver.
    explicit Receiver(uint32_t flags = 0u);

    // Destroys the receiver.
    //
    // This object must not be destroyed until all packets destined for it
    // have been delivered or the asynchronous dispatcher itself has been
    // destroyed.
    ~Receiver();

    // Gets or sets the handler to invoke when a packet is received.
    // Must be set before queuing any packets.
    const Handler& handler() const { return handler_; }
    void set_handler(Handler handler) { handler_ = fbl::move(handler); }

    // Valid flags: None, set to zero.
    uint32_t flags() const { return async_receiver_t::flags; }
    void set_flags(uint32_t flags) { async_receiver_t::flags = flags; }

    // Enqueues a packet of data for delivery to the receiver.
    //
    // See |async_queue_packet()| for details.
    zx_status_t Queue(async_t* async, const zx_packet_user_t* data = nullptr);

private:
    static void CallHandler(async_t* async, async_receiver_t* receiver,
                            zx_status_t status, const zx_packet_user_t* data);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Receiver);
};

} // namespace async
