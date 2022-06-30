// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/wire_coding_traits.h>

namespace fidl::internal {

void WireDecodeUnknownEnvelope(WireDecoder* decoder, WirePosition position) {
  const fidl_envelope_v2_t* envelope = position.As<fidl_envelope_v2_t>();
  if (envelope->flags == 0) {
    if (envelope->num_bytes % FIDL_ALIGNMENT != 0) {
      decoder->SetError(kCodingErrorInvalidNumBytesSpecifiedInEnvelope);
      return;
    }
    WirePosition new_position;
    if (!decoder->Alloc(envelope->num_bytes, &new_position)) {
      return;
    }
  } else if (envelope->flags != FIDL_ENVELOPE_FLAGS_INLINING_MASK) {
    decoder->SetError(kCodingErrorInvalidInlineBit);
    return;
  }
  decoder->CloseNextNHandles(envelope->num_handles);
}

fitx::result<fidl::Error, WireEncoder::Result> WireEncode(
    size_t inline_size, TopLevelEncodeFn encode_fn, const CodingConfig* coding_config, void* value,
    zx_channel_iovec_t* iovecs, size_t iovec_capacity, fidl_handle_t* handles,
    fidl_handle_metadata_t* handle_metadata, size_t handle_capacity, uint8_t* backing_buffer,
    size_t backing_buffer_capacity) {
  if (unlikely(value == nullptr)) {
    return fitx::error(fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullIovecBuffer));
  }
  if (unlikely(iovecs == nullptr)) {
    return fitx::error(fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullIovecBuffer));
  }
  if (unlikely(handle_capacity > 0 && handles == nullptr)) {
    return fitx::error(
        fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullHandleBufferButNonzeroCount));
  }
  if (unlikely(backing_buffer == nullptr)) {
    return fitx::error(fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullByteBuffer));
  }
  WireEncoder encoder(coding_config, iovecs, iovec_capacity, handles, handle_metadata,
                      handle_capacity, backing_buffer, backing_buffer_capacity);
  WirePosition position;
  if (likely(encoder.Alloc(inline_size, &position))) {
    encode_fn(&encoder, value, position);
  }
  return encoder.Finish();
}

fidl::Status WireDecode(size_t inline_size, TopLevelDecodeFn decode_fn,
                        const CodingConfig* coding_config, uint8_t* bytes, size_t num_bytes,
                        fidl_handle_t* handles, fidl_handle_metadata_t* handle_metadata,
                        size_t num_handles) {
  if (unlikely(bytes == nullptr)) {
    return fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullByteBuffer);
  }
  if (unlikely(num_handles > 0 && handles == nullptr)) {
    return fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS,
                                     kCodingErrorNullHandleBufferButNonzeroCount);
  }
  WireDecoder decoder(coding_config, bytes, num_bytes, handles, handle_metadata, num_handles);
  WirePosition position;
  if (likely(decoder.Alloc(inline_size, &position))) {
    decode_fn(&decoder, position);
  }
  if (unlikely(decoder.CurrentLength() < num_bytes)) {
    decoder.SetError(kCodingErrorNotAllBytesConsumed);
  }
  if (unlikely(decoder.CurrentHandleCount() < num_handles)) {
    decoder.SetError(kCodingErrorNotAllHandlesConsumed);
  }
  return decoder.Finish();
}

}  // namespace fidl::internal
