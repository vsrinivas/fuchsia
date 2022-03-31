// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/natural_server_messenger.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/transaction.h>

#include "lib/fidl/llcpp/internal/transport.h"

namespace fidl {
namespace internal {

void NaturalServerMessenger::SendReply(fidl::OutgoingMessage message,
                                       OutgoingTransportContext context) const {
  completer_base_->SendReply(&message, std::move(context));
}

}  // namespace internal
}  // namespace fidl
