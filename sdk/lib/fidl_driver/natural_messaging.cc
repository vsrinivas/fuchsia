// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl_driver/cpp/natural_messaging.h>

namespace fdf::internal {

const char* const kFailedToCreateDriverArena = "failed to create driver arena";

fidl::OutgoingMessage MoveToArena(fidl::OutgoingMessage& message, const fdf::Arena& arena) {
  if (!message.ok()) {
    return fidl::OutgoingMessage(message.error());
  }

  auto bytes = message.CopyBytes();
  void* bytes_on_arena = arena.Allocate(bytes.size());
  memcpy(bytes_on_arena, bytes.data(), bytes.size());

  void* handles_on_arena = arena.Allocate(message.handle_actual() * sizeof(fidl_handle_t));
  memcpy(handles_on_arena, message.handles(), message.handle_actual() * sizeof(fidl_handle_t));

  uint32_t handle_actual = message.handle_actual();
  message.ReleaseHandles();

  fidl::OutgoingMessage::InternalByteBackedConstructorArgs args = {
      .transport_vtable = &fidl::internal::DriverTransport::VTable,
      .bytes = static_cast<uint8_t*>(bytes_on_arena),
      .num_bytes = static_cast<uint32_t>(bytes.size()),
      .handles = static_cast<fidl_handle_t*>(handles_on_arena),
      .handle_metadata = nullptr,
      .num_handles = handle_actual,
      .is_transactional = message.is_transactional(),
  };
  return fidl::OutgoingMessage::Create_InternalMayBreak(args);
}

}  // namespace fdf::internal
