// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_decoder.h>
#include <lib/fidl/cpp/wire/message.h>

#include <utility>

namespace fidl::internal {

NaturalDecoder::NaturalDecoder(fidl::EncodedMessage message,
                               fidl::internal::WireFormatVersion wire_format_version)
    : body_(std::move(message)), wire_format_version_(wire_format_version) {}

NaturalDecoder::~NaturalDecoder() = default;

void NaturalDecoder::DecodeUnknownEnvelopeOptional(size_t offset) {
  static_assert(sizeof(fidl_envelope_v2_t) == sizeof(uint64_t));
  const fidl_envelope_v2_t* envelope = GetPtr<fidl_envelope_v2_t>(offset);
  if (FidlIsZeroEnvelope(envelope)) {
    return;
  }
  DecodeUnknownEnvelope(envelope);
}

void NaturalDecoder::DecodeUnknownEnvelopeRequired(size_t offset) {
  static_assert(sizeof(fidl_envelope_v2_t) == sizeof(uint64_t));
  const fidl_envelope_v2_t* envelope = GetPtr<fidl_envelope_v2_t>(offset);
  if (unlikely(FidlIsZeroEnvelope(envelope))) {
    SetError(kCodingErrorInvalidUnionTag);
    return;
  }
  DecodeUnknownEnvelope(envelope);
}

void NaturalDecoder::DecodeUnknownEnvelope(const fidl_envelope_v2_t* envelope) {
  if (envelope->flags == 0) {
    if (envelope->num_bytes % FIDL_ALIGNMENT != 0) {
      SetError(kCodingErrorInvalidNumBytesSpecifiedInEnvelope);
      return;
    }
    size_t envelope_content_offset;
    if (!Alloc(envelope->num_bytes, &envelope_content_offset)) {
      return;
    }
  } else if (envelope->flags != FIDL_ENVELOPE_FLAGS_INLINING_MASK) {
    SetError(kCodingErrorInvalidInlineBit);
    return;
  }
  CloseNextHandles(envelope->num_handles);
}

void NaturalDecoder::CloseNextHandles(size_t count) {
  if (unlikely(count > body_.handle_actual() - handle_index_)) {
    SetError(kCodingErrorInvalidNumHandlesSpecifiedInEnvelope);
    return;
  }

  coding_config()->close_many(&body_.handles()[handle_index_], count);
  const size_t end = handle_index_ + count;
  for (; handle_index_ < end; handle_index_++) {
    body_.handles()[handle_index_] = FIDL_HANDLE_INVALID;
  }
}

}  // namespace fidl::internal
