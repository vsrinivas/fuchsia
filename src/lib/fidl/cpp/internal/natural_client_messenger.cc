// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/status.h>

namespace fidl {
namespace internal {

void NaturalClientMessenger::TwoWay(fidl::OutgoingMessage message,
                                    fidl::internal::ResponseContext* context,
                                    fidl::WriteOptions write_options) const {
  client_base_->SendTwoWay(message, context, std::move(write_options));
}

fidl::Status NaturalClientMessenger::OneWay(fidl::OutgoingMessage message,
                                            fidl::WriteOptions write_options) const {
  return client_base_->SendOneWay(message, std::move(write_options));
}

}  // namespace internal
}  // namespace fidl
