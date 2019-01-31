// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/message_queue/cpp/message_sender_client.h>

#include <lib/fsl/vmo/strings.h>

namespace modular {

MessageSenderClient::MessageSenderClient() = default;

void MessageSenderClient::Send(fxl::StringView msg) {
  FXL_DCHECK(sender_);

  fsl::SizedVmo vmo;
  FXL_CHECK(fsl::VmoFromString(msg, &vmo));
  sender_->Send(std::move(vmo).ToTransport());
}

fidl::InterfaceRequest<fuchsia::modular::MessageSender>
MessageSenderClient::NewRequest() {
  return sender_.NewRequest();
}

}  // namespace modular