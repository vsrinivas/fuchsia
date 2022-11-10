// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/trace.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstring>
#include <string>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/server.h>
#include <zircon/syscalls.h>
#else
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#endif  // __Fuchsia__

namespace fidl {

OutgoingToIncomingMessage::OutgoingToIncomingMessage(OutgoingMessage& input)
    : incoming_message_(
          ConversionImpl(input, buf_bytes_, buf_handles_, buf_handle_metadata_, status_)) {}

[[nodiscard]] std::string OutgoingToIncomingMessage::FormatDescription() const {
  return status_.FormatDescription();
}

EncodedMessage OutgoingToIncomingMessage::ConversionImpl(
    OutgoingMessage& input, OutgoingMessage::CopiedBytes& buf_bytes,
    std::unique_ptr<zx_handle_t[]>& buf_handles,
    // TODO(fxbug.dev/85734) Remove channel-specific logic.
    std::unique_ptr<fidl_channel_handle_metadata_t[]>& buf_handle_metadata,
    fidl::Status& out_status) {
  ZX_ASSERT(!input.is_transactional());

  zx_handle_t* handles = input.handles();
  fidl_channel_handle_metadata_t* handle_metadata =
      input.handle_metadata<fidl::internal::ChannelTransport>();
  uint32_t num_handles = input.handle_actual();
  input.ReleaseHandles();

  // Note: it may be possible to remove these allocations.
  buf_handles = std::make_unique<zx_handle_t[]>(num_handles);
  buf_handle_metadata = std::make_unique<fidl_channel_handle_metadata_t[]>(num_handles);
  for (uint32_t i = 0; i < num_handles; i++) {
    const char* error;
    zx_status_t status = FidlEnsureActualHandleRights(&handles[i], handle_metadata[i].obj_type,
                                                      handle_metadata[i].rights, &error);
    if (status != ZX_OK) {
      FidlHandleCloseMany(handles, num_handles);
      FidlHandleCloseMany(buf_handles.get(), num_handles);
      out_status = fidl::Status::EncodeError(status);
      return fidl::EncodedMessage::Create({});
    }
    buf_handles[i] = handles[i];
    buf_handle_metadata[i] = handle_metadata[i];
  }

  buf_bytes = input.CopyBytes();
  out_status = fidl::Status::Ok();
  return fidl::EncodedMessage::Create(cpp20::span<uint8_t>{buf_bytes}, buf_handles.get(),
                                      buf_handle_metadata.get(), num_handles);
}

}  // namespace fidl
