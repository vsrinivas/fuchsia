// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/message_receiver_client.h"

#include <utility>

namespace modular {

MessageReceiverClient::MessageReceiverClient(
    modular::MessageQueue* const mq,
    MessageReceiverClientCallback callback)
    : callback_(std::move(callback)), receiver_(this) {
  mq->RegisterReceiver(receiver_.NewBinding());
}

MessageReceiverClient::~MessageReceiverClient() = default;

void MessageReceiverClient::OnReceive(const fidl::String& message,
                                      const OnReceiveCallback& ack) {
  callback_(message, ack);
}

}  // namespace modular
