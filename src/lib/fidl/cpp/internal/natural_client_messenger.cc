// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/result.h>

namespace fidl {
namespace internal {

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
void NaturalClientMessenger::TwoWay(const fidl_type_t* type, HLCPPOutgoingMessage&& message,
                                    fidl::internal::ResponseContext* context) const {
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  auto outgoing =
      ConvertFromHLCPPOutgoingMessage(type, std::move(message), handles, handle_metadata);
  client_base_->SendTwoWay(outgoing, context);
}

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
fidl::Result NaturalClientMessenger::OneWay(const fidl_type_t* type,
                                            HLCPPOutgoingMessage&& message) const {
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  auto outgoing =
      ConvertFromHLCPPOutgoingMessage(type, std::move(message), handles, handle_metadata);
  return client_base_->SendOneWay(outgoing);
}

}  // namespace internal
}  // namespace fidl
