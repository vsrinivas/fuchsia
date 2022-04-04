// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/natural_message_encoder.h>
#include <lib/fidl/txn_header.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

namespace fidl::internal {

NaturalMessageEncoder::NaturalMessageEncoder(const TransportVTable* vtable, uint64_t ordinal)
    : body_encoder_(vtable, WireFormatVersion::kV2) {
  EncodeMessageHeader(ordinal);
}

fidl::OutgoingMessage NaturalMessageEncoder::GetMessage() {
  fitx::result result = std::move(body_encoder_).GetBodyView();
  if (result.is_error()) {
    return fidl::OutgoingMessage(result.error_value());
  }

  NaturalBodyEncoder::BodyView& chunk = result.value();
  return fidl::OutgoingMessage::Create_InternalMayBreak(
      fidl::OutgoingMessage::InternalByteBackedConstructorArgs{
          .transport_vtable = chunk.vtable,
          .bytes = chunk.bytes.data(),
          .num_bytes = static_cast<uint32_t>(chunk.bytes.size()),
          .handles = chunk.handles,
          .handle_metadata = chunk.handle_metadata,
          .num_handles = chunk.num_handles,
          .is_transactional = true,
      });
}

void NaturalMessageEncoder::Reset(uint64_t ordinal) {
  body_encoder_.Reset();
  EncodeMessageHeader(ordinal);
}

void NaturalMessageEncoder::EncodeMessageHeader(uint64_t ordinal) {
  size_t offset = body_encoder_.Alloc(sizeof(fidl_message_header_t));
  fidl_message_header_t* header = body_encoder_.GetPtr<fidl_message_header_t>(offset);
  fidl_init_txn_header(header, 0, ordinal);
  if (body_encoder_.wire_format() == internal::WireFormatVersion::kV2) {
    header->flags[0] |= FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2;
  }
}

}  // namespace fidl::internal
