// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/natural_message_encoder.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

namespace fidl::internal {

NaturalMessageEncoder::NaturalMessageEncoder(const TransportVTable* vtable, uint64_t ordinal,
                                             MessageDynamicFlags dynamic_flags)
    : body_encoder_(vtable, WireFormatVersion::kV2) {
  EncodeMessageHeader(ordinal, dynamic_flags);
}

fidl::OutgoingMessage NaturalMessageEncoder::GetMessage() {
  return std::move(body_encoder_)
      .GetOutgoingMessage(NaturalBodyEncoder::MessageType::kTransactional);
}

void NaturalMessageEncoder::Reset(uint64_t ordinal, MessageDynamicFlags dynamic_flags) {
  body_encoder_.Reset();
  EncodeMessageHeader(ordinal, dynamic_flags);
}

void NaturalMessageEncoder::EncodeMessageHeader(uint64_t ordinal,
                                                MessageDynamicFlags dynamic_flags) {
  size_t offset = body_encoder_.Alloc(sizeof(fidl_message_header_t));
  fidl_message_header_t* header = body_encoder_.GetPtr<fidl_message_header_t>(offset);
  fidl::InitTxnHeader(header, 0, ordinal, dynamic_flags);
  if (body_encoder_.wire_format() == internal::WireFormatVersion::kV2) {
    header->at_rest_flags[0] |= FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2;
  }
}

}  // namespace fidl::internal
