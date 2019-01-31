// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/vmo/strings.h>
#include <lib/message_queue/cpp/message_queue_client.h>

namespace modular {

MessageQueueClient::MessageQueueClient() : reader_(this) {}

fidl::InterfaceRequest<fuchsia::modular::MessageQueue>
MessageQueueClient::NewRequest() {
  if (reader_.is_bound()) {
    reader_.Unbind();
    receiver_ = nullptr;
  }

  return queue_.NewRequest();
}

void MessageQueueClient::GetToken(
    std::function<void(fidl::StringPtr)> callback) {
  queue_->GetToken(callback);
}

void MessageQueueClient::RegisterReceiver(ReceiverCallback receiver) {
  receiver_ = std::move(receiver);

  if (!receiver_) {
    reader_.Unbind();
    return;
  }

  if (!reader_.is_bound()) {
    queue_->RegisterReceiver(reader_.NewBinding());
  }
}

// |fuchsia::modular::MessageReader|
void MessageQueueClient::OnReceive(fuchsia::mem::Buffer message,
                                   std::function<void()> ack) {
  FXL_DCHECK(reader_.is_bound());

  std::string str;
  FXL_CHECK(fsl::StringFromVmo(message, &str));

  receiver_(std::move(str), std::move(ack));
}

}  // namespace modular