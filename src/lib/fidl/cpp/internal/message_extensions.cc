// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/message_extensions.h>
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
      static_cast<fidl_channel_handle_metadata_t*>(c_msg.handle_metadata), c_msg.num_handles,
      ::fidl::IncomingMessage::kSkipMessageHeaderValidation);
}

::fidl::HLCPPIncomingMessage ConvertToHLCPPIncomingMessage(
    fidl::IncomingMessage message,
    cpp20::span<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES> handle_storage) {
  fidl_incoming_msg_t c_msg = std::move(message).ReleaseToEncodedCMessage();
  auto* handle_metadata = static_cast<fidl_channel_handle_metadata_t*>(c_msg.handle_metadata);
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

}  // namespace internal
}  // namespace fidl
