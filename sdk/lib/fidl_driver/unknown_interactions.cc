// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/arena.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/fidl_driver/cpp/unknown_interactions.h>

namespace fidl::internal {

void SendDriverUnknownInteractionReply(UnknownInteractionReply reply, ::fidl::Transaction* txn) {
  auto arena = fdf::Arena::Create(0, "");
  if (!arena.is_ok()) {
    txn->InternalError(::fidl::UnbindInfo{::fidl::Status::TransportError(arena.status_value())},
                       fidl::ErrorOrigin::kSend);
    return;
  }

  void* arena_bytes = arena->Allocate(sizeof(reply));
  ::std::memcpy(arena_bytes, &reply, sizeof(reply));

  ::fidl::OutgoingMessage msg = ::fidl::OutgoingMessage::Create_InternalMayBreak(
      ::fidl::OutgoingMessage::InternalByteBackedConstructorArgs{
          .transport_vtable = &DriverTransport::VTable,
          .bytes = static_cast<uint8_t*>(arena_bytes),
          .num_bytes = sizeof(reply),
          .handles = nullptr,
          .handle_metadata = nullptr,
          .num_handles = 0,
          .is_transactional = true,
      });

  ::fidl::internal::OutgoingTransportContext context =
      ::fidl::internal::OutgoingTransportContext::Create<::fidl::internal::DriverTransport>(
          arena->get());

  txn->Reply(&msg, {.outgoing_transport_context = std::move(context)});
}
}  // namespace fidl::internal
