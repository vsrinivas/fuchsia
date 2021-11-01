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

fidl::OutgoingMessage ConvertFromHLCPP(const fidl_type_t* type, HLCPPOutgoingMessage&& message,
                                       zx_handle_t* handles,
                                       fidl_channel_handle_metadata_t* handle_metadata) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    return fidl::OutgoingMessage(fidl::Result::EncodeError(status, error_msg));
  }

  for (size_t i = 0; i < message.handles().actual(); i++) {
    zx_handle_disposition_t handle_disposition = message.handles().data()[i];
    handles[i] = handle_disposition.handle;
    handle_metadata[i] = {
        .obj_type = handle_disposition.type,
        .rights = handle_disposition.rights,
    };
  }

  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
      .byte =
          {
              .transport_type = FIDL_TRANSPORT_TYPE_CHANNEL,
              .bytes = message.bytes().data(),
              .handles = handles,
              .handle_metadata = handle_metadata,
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
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  auto outgoing = ConvertFromHLCPP(type, std::move(message), handles, handle_metadata);
  client_base_->SendTwoWay(outgoing, context);
}

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
fidl::Result NaturalClientMessenger::OneWay(const fidl_type_t* type,
                                            HLCPPOutgoingMessage&& message) const {
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  auto outgoing = ConvertFromHLCPP(type, std::move(message), handles, handle_metadata);
  return client_base_->SendOneWay(outgoing);
}

}  // namespace internal
}  // namespace fidl
