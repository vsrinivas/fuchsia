// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>

namespace fidl::internal {

const CodingConfig kNullCodingConfig = {
    .max_iovecs_write = 1,
    .handle_metadata_stride = 0,
    .encode_process_handle = nullptr,
    .decode_process_handle = nullptr,
    .close = [](fidl_handle_t handle) { ZX_PANIC("Should not have handles"); },
    .close_many =
        [](const fidl_handle_t* handles, size_t num_handles) {
          ZX_ASSERT_MSG(num_handles == 0, "Should not have handles");
        },
};

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

fit::result<fidl::Error, WireEncoder::Result> WireEncode(
    size_t inline_size, TopLevelEncodeFn encode_fn, const CodingConfig* coding_config, void* value,
    zx_channel_iovec_t* iovecs, size_t iovec_capacity, fidl_handle_t* handles,
    fidl_handle_metadata_t* handle_metadata, size_t handle_capacity, uint8_t* backing_buffer,
    size_t backing_buffer_capacity) {
  if (unlikely(value == nullptr)) {
    return fit::error(fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullIovecBuffer));
  }
  if (unlikely(iovecs == nullptr)) {
    return fit::error(fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullIovecBuffer));
  }
  if (unlikely(handle_capacity > 0 && handles == nullptr)) {
    return fit::error(
        fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullHandleBufferButNonzeroCount));
  }
  if (unlikely(backing_buffer == nullptr)) {
    return fit::error(fidl::Error::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNullByteBuffer));
  }
  WireEncoder encoder(coding_config, iovecs, iovec_capacity, handles, handle_metadata,
                      handle_capacity, backing_buffer, backing_buffer_capacity);
  WirePosition position;
  if (likely(encoder.Alloc(inline_size, &position))) {
    encode_fn(&encoder, value, position);
  }
  return encoder.Finish();
}

fidl::Status WireDecode(::fidl::WireFormatMetadata metadata, bool contains_envelope,
                        size_t inline_size, TopLevelDecodeFn decode_fn,
                        ::fidl::EncodedMessage& message) {
  if (fidl::Status status = EnsureSupportedWireFormat(metadata); !status.ok()) {
    std::move(message).CloseHandles();
    return status;
  }

  uint8_t* bytes = message.bytes().data();
  size_t num_bytes = message.bytes().size();
  fidl_handle_t* handles = message.handles();
  fidl_handle_metadata_t* handle_metadata = message.raw_handle_metadata();
  size_t num_handles = message.handle_actual();
  const internal::CodingConfig* coding_config =
      message.transport_vtable() ? message.transport_vtable()->encoding_configuration
                                 : &internal::kNullCodingConfig;

  if (unlikely(bytes == nullptr)) {
    std::move(message).CloseHandles();
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

  std::move(message).ReleaseHandles();
  // Handles are closed in |Finish| in case of error.
  return decoder.Finish();
}

::fidl::Status EnsureSupportedWireFormat(::fidl::WireFormatMetadata metadata) {
  if (unlikely(!metadata.is_valid())) {
    return ::fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorInvalidWireFormatMetadata);
  }
  if (unlikely(metadata.wire_format_version() == fidl::internal::WireFormatVersion::kV1)) {
    return ::fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorDoesNotSupportV1Envelopes);
  }
  if (unlikely(metadata.wire_format_version() != fidl::internal::WireFormatVersion::kV2)) {
    return ::fidl::Error::DecodeError(ZX_ERR_NOT_SUPPORTED,
                                      kCodingErrorUnsupportedWireFormatVersion);
  }
  return ::fidl::Status::Ok();
}

}  // namespace fidl::internal
