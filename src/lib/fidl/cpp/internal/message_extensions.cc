// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

}  // namespace internal
}  // namespace fidl
