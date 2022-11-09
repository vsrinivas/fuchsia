// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>

namespace fidl::internal {

fidl::Status NaturalDecode(::fidl::WireFormatMetadata metadata, bool contains_envelope,
                           size_t inline_size, NaturalTopLevelDecodeFn decode_fn,
                           ::fidl::EncodedMessage& message, void* value) {
  if (fidl::Status status = EnsureSupportedWireFormat(metadata); !status.ok()) {
    std::move(message).CloseHandles();
    return status;
  }

  size_t message_byte_actual = message.bytes().size();
  uint32_t message_handle_actual = message.handle_actual();
  ::fidl::internal::NaturalDecoder decoder(std::move(message), metadata.wire_format_version());
  size_t offset;
  if (unlikely(!decoder.Alloc(inline_size, &offset))) {
    return ::fidl::Error::DecodeError(decoder.status(), decoder.error());
  }

  decode_fn(&decoder, value, offset);
  if (unlikely(decoder.status() != ZX_OK)) {
    return ::fidl::Error::DecodeError(decoder.status(), decoder.error());
  }
  if (unlikely(decoder.CurrentLength() != message_byte_actual)) {
    return ::fidl::Error::DecodeError(ZX_ERR_INTERNAL, kCodingErrorNotAllBytesConsumed);
  }
  if (unlikely(decoder.CurrentHandleCount() != message_handle_actual)) {
    return ::fidl::Error::DecodeError(ZX_ERR_INTERNAL, kCodingErrorNotAllHandlesConsumed);
  }
  return ::fidl::Status::Ok();
}

}  // namespace fidl::internal
