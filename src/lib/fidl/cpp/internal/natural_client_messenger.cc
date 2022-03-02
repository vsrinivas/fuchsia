// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/result.h>

namespace fidl {
namespace internal {

void NaturalClientMessenger::TwoWay(fidl::OutgoingMessage message,
                                    fidl::internal::ResponseContext* context) const {
  client_base_->SendTwoWay(message, context);
}

fidl::Result NaturalClientMessenger::OneWay(fidl::OutgoingMessage message) const {
  return client_base_->SendOneWay(message);
}

}  // namespace internal
}  // namespace fidl
