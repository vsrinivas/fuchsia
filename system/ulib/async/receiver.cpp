// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/receiver.h>

namespace async {

Receiver::Receiver()
    : receiver_{{ASYNC_STATE_INIT}, &Receiver::CallHandler} {}

Receiver::Receiver(Handler handler)
    : receiver_{{ASYNC_STATE_INIT}, &Receiver::CallHandler},
      handler_(static_cast<Handler&&>(handler)) {}

Receiver::~Receiver() = default;

zx_status_t Receiver::QueuePacket(async_t* async, const zx_packet_user_t* data) {
    return async_queue_packet(async, &receiver_, data);
}

zx_status_t Receiver::QueuePacketOrReportError(async_t* async, const zx_packet_user_t* data) {
    return async_queue_packet_or_report_error(async, &receiver_, data);
}

void Receiver::CallHandler(async_t* async, async_receiver_t* receiver,
                           zx_status_t status, const zx_packet_user_t* data) {
    static_assert(offsetof(Receiver, receiver_) == 0, "");
    auto self = reinterpret_cast<Receiver*>(receiver);
    self->handler_(async, self, status, data);
}

} // namespace async
