// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/encoder.h>
#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

namespace fidl {
namespace internal {

::fidl::IncomingMessage SkipTransactionHeader(::fidl::IncomingMessage message) {
  ZX_ASSERT(message.is_transactional());
  fidl_incoming_msg_t c_msg = std::move(message).ReleaseToEncodedCMessage();
  ZX_ASSERT(c_msg.num_bytes >= sizeof(fidl_message_header_t));
  return ::fidl::IncomingMessage::Create(
      static_cast<uint8_t*>(c_msg.bytes) + sizeof(fidl_message_header_t),
      c_msg.num_bytes - static_cast<uint32_t>(sizeof(fidl_message_header_t)), c_msg.handles,
      reinterpret_cast<fidl_channel_handle_metadata_t*>(c_msg.handle_metadata), c_msg.num_handles,
      ::fidl::IncomingMessage::kSkipMessageHeaderValidation);
}

::fidl::HLCPPIncomingMessage ConvertToHLCPPIncomingMessage(
    fidl::IncomingMessage message,
    cpp20::span<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES> handle_storage) {
  ZX_ASSERT(message.is_transactional());
  fidl_incoming_msg_t c_msg = std::move(message).ReleaseToEncodedCMessage();
  auto* handle_metadata = reinterpret_cast<fidl_channel_handle_metadata_t*>(c_msg.handle_metadata);
  for (size_t i = 0; i < c_msg.num_handles && i < ZX_CHANNEL_MAX_MSG_HANDLES; i++) {
    handle_storage[i] = zx_handle_info_t{
        .handle = c_msg.handles[i],
        .type = handle_metadata[i].obj_type,
        .rights = handle_metadata[i].rights,
        .unused = 0,
    };
  }

  return ::fidl::HLCPPIncomingMessage{
      ::fidl::BytePart{static_cast<uint8_t*>(c_msg.bytes), c_msg.num_bytes, c_msg.num_bytes},
      ::fidl::HandleInfoPart{handle_storage.data(), c_msg.num_handles, c_msg.num_handles},
  };
}

::fidl::HLCPPIncomingBody ConvertToHLCPPIncomingBody(
    fidl::IncomingMessage message,
    ::cpp20::span<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES> handle_storage) {
  ZX_ASSERT(!message.is_transactional());
  fidl_incoming_msg_t c_msg = std::move(message).ReleaseToEncodedCMessage();
  auto* handle_metadata = reinterpret_cast<fidl_channel_handle_metadata_t*>(c_msg.handle_metadata);
  for (size_t i = 0; i < c_msg.num_handles && i < ZX_CHANNEL_MAX_MSG_HANDLES; i++) {
    handle_storage[i] = zx_handle_info_t{
        .handle = c_msg.handles[i],
        .type = handle_metadata[i].obj_type,
        .rights = handle_metadata[i].rights,
        .unused = 0,
    };
  }

  return ::fidl::HLCPPIncomingBody(
      ::fidl::BytePart(static_cast<uint8_t*>(c_msg.bytes), c_msg.num_bytes, c_msg.num_bytes),
      ::fidl::HandleInfoPart(handle_storage.data(), c_msg.num_handles, c_msg.num_handles));
}

::fidl::OutgoingMessage ConvertFromHLCPPOutgoingMessage(
    const fidl_type_t* type, HLCPPOutgoingMessage&& message, zx_handle_t* handles,
    fidl_channel_handle_metadata_t* handle_metadata) {
  if (type != nullptr) {
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
  } else if (unlikely(!message.has_only_header())) {
    return fidl::OutgoingMessage(fidl::Result::EncodeError(ZX_ERR_INVALID_ARGS));
  }

  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
      .byte =
          {
              .bytes = message.bytes().data(),
              .handles = handles,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
              .num_bytes = message.bytes().actual(),
              .num_handles = message.handles().actual(),
          },
  };
  // Ownership will be transferred to |fidl::OutgoingMessage|.
  message.ClearHandlesUnsafe();
  return fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
}

::fidl::OutgoingMessage ConvertFromHLCPPOutgoingBody(
    const internal::WireFormatVersion& wire_format_version, const fidl_type_t* type,
    HLCPPOutgoingBody&& body, zx_handle_t* handles,
    fidl_channel_handle_metadata_t* handle_metadata) {
  const char* error_msg = nullptr;
  zx_status_t status = body.Validate(wire_format_version, type, &error_msg);
  if (status != ZX_OK) {
    return fidl::OutgoingMessage(fidl::Result::EncodeError(status, error_msg));
  }

  for (size_t i = 0; i < body.handles().actual(); i++) {
    zx_handle_disposition_t handle_disposition = body.handles().data()[i];
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
              .bytes = body.bytes().data(),
              .handles = handles,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
              .num_bytes = body.bytes().actual(),
              .num_handles = body.handles().actual(),
          },
  };
  // Ownership will be transferred to |fidl::OutgoingMessage|.
  body.ClearHandlesUnsafe();
  return fidl::OutgoingMessage::FromEncodedCValue(&c_msg);
}

}  // namespace internal
}  // namespace fidl
