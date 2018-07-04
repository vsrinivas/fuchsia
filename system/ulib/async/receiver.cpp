// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/receiver.h>

namespace async {

ReceiverBase::ReceiverBase(async_receiver_handler_t* handler)
    : receiver_{{ASYNC_STATE_INIT}, handler} {}

ReceiverBase::~ReceiverBase() = default;

zx_status_t ReceiverBase::QueuePacket(async_dispatcher_t* dispatcher, const zx_packet_user_t* data) {
    return async_queue_packet(dispatcher, &receiver_, data);
}

Receiver::Receiver(Handler handler)
    : ReceiverBase(&Receiver::CallHandler), handler_(fbl::move(handler)) {}

Receiver::~Receiver() = default;

void Receiver::CallHandler(async_dispatcher_t* dispatcher, async_receiver_t* receiver,
                           zx_status_t status, const zx_packet_user_t* data) {
    auto self = Dispatch<Receiver>(receiver);
    self->handler_(dispatcher, self, status, data);
}

} // namespace async
