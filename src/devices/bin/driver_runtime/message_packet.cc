// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/message_packet.h"

#include <lib/fdf/channel.h>

#include "src/devices/bin/driver_runtime/arena.h"
#include "src/devices/bin/driver_runtime/handle.h"

namespace driver_runtime {

// static
MessagePacketOwner MessagePacket::Create(fbl::RefPtr<fdf_arena> arena, void* data,
                                         uint32_t num_bytes, zx_handle_t* handles,
                                         uint32_t num_handles) {
  void* message_packet = nullptr;
  if (arena) {
    message_packet = arena->Allocate(sizeof(MessagePacket));
  } else {
    // The user wrote an empty message that did not provide an arena.
    message_packet = calloc(1, sizeof(MessagePacket));
  }
  if (!message_packet) {
    return nullptr;
  }
  return MessagePacketOwner(
      new (message_packet) MessagePacket(std::move(arena), data, num_bytes, handles, num_handles));
}

// static
void MessagePacket::Delete(MessagePacket* message_packet) {
  // Since the message packet may be allocated from the arena, keep it alive
  // until we finish destruction.
  fbl::RefPtr<fdf_arena_t> arena = std::move(message_packet->arena_);

  message_packet->~MessagePacket();

  if (!arena) {
    // The user wrote an empty message that did not provide an arena.
    // TODO(fxbug.dev/86856): we should consider recycling deleted packets.
    free(message_packet);
  }
}

MessagePacket::~MessagePacket() {
  ZX_ASSERT(!InContainer());
  if (handles_) {
    for (size_t i = 0; i != num_handles_; i++) {
      if (Handle::IsFdfHandle(handles_[i])) {
        fdf_handle_close(handles_[i]);
      } else {
        zx_handle_close(handles_[i]);
      }
    }
  }
}

void MessagePacket::CopyOut(fdf_arena_t** out_arena, void** out_data, uint32_t* out_num_bytes,
                            zx_handle_t** out_handles, uint32_t* out_num_handles) {
  if (out_arena) {
    fbl::RefPtr<fdf_arena> arena = this->arena();
    // The reference is dropped when the user calls fbl_arena::Destroy.
    *out_arena = fbl::ExportToRawPtr(&arena);
  }
  if (out_data) {
    TakeData(out_data);
  }
  if (out_num_bytes) {
    *out_num_bytes = num_bytes();
  }
  if (out_handles) {
    TakeHandles(out_handles);
  }
  if (out_num_handles) {
    *out_num_handles = num_handles();
  }
}

}  // namespace driver_runtime
