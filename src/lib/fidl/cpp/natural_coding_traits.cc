// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/wire/incoming_message.h>

namespace fidl::internal {

fidl::Status NaturalDecode(::fidl::WireFormatMetadata metadata, bool contains_envelope,
                           size_t inline_size, NaturalTopLevelDecodeFn decode_fn,
                           ::fidl::EncodedMessage& message, void* value) {
  if (unlikely(!metadata.is_valid())) {
    std::move(message).CloseHandles();
    return ::fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorInvalidWireFormatMetadata);
  }
  // Old versions of the C bindings will send wire format V1 payloads that are compatible
  // with wire format V2 (they don't contain envelopes). Confirm that V1 payloads don't
  // contain envelopes and are compatible with V2.
  // TODO(fxbug.dev/99738): Remove this logic.
  if (unlikely(contains_envelope &&
               metadata.wire_format_version() == fidl::internal::WireFormatVersion::kV1)) {
    std::move(message).CloseHandles();
    return Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorDoesNotSupportV1Envelopes);
  }
  // TODO(fxbug.dev/99738): Drop "non-envelope V1" support.
  if (unlikely(metadata.wire_format_version() != fidl::internal::WireFormatVersion::kV1 &&
               metadata.wire_format_version() != fidl::internal::WireFormatVersion::kV2)) {
    std::move(message).CloseHandles();
    return ::fidl::Error::DecodeError(ZX_ERR_NOT_SUPPORTED,
                                      kCodingErrorUnsupportedWireFormatVersion);
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
