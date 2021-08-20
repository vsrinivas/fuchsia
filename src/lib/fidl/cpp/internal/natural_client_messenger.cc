// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/result.h>

namespace fidl {
namespace internal {

namespace {

fidl::OutgoingMessage ConvertFromHLCPP(const fidl_type_t* type, HLCPPOutgoingMessage&& message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    return fidl::OutgoingMessage(fidl::Result::EncodeError(status, error_msg));
  }

  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
      .byte =
          {
              .bytes = message.bytes().data(),
              .handles = message.handles().begin(),
              .num_bytes = message.bytes().actual(),
              .num_handles = message.handles().actual(),
          },
  };
  // Ownership will be transferred to |fidl::OutgoingMessage|.
  message.ClearHandlesUnsafe();
  return fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
}

}  // namespace

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
void NaturalClientMessenger::TwoWay(const fidl_type_t* type, HLCPPOutgoingMessage&& message,
                                    fidl::internal::ResponseContext* context) const {
  auto outgoing = ConvertFromHLCPP(type, std::move(message));
  client_base_->SendTwoWay(outgoing, context);
}

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
fidl::Result NaturalClientMessenger::OneWay(const fidl_type_t* type,
                                            HLCPPOutgoingMessage&& message) const {
  auto outgoing = ConvertFromHLCPP(type, std::move(message));
  return client_base_->SendOneWay(outgoing);
}

}  // namespace internal
}  // namespace fidl
