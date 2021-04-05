// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_
#define LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_

#include <lib/fidl/llcpp/message.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

namespace fidl {
namespace fuzzing {

// DecoderEncoderProgress encodes the progress of attempted `DecoderEncoder` implementations. Order
// matters here, that is, subsequent (larger) enum values imply further progress in a
// `DecoderEncoder`.
enum DecoderEncoderProgress {
  // First operation (decode) failed.
  NoProgress = 0,

  // First attempt to decode succeeded.
  FirstDecodeSuccess,

  // First attempt to re-encode decoded message succeeded.
  FirstEncodeSuccess,

  // Additional checks on handle rights triggered by `::fidl::OutgoingToIncomingMessage` succeeded.
  FirstEncodeVerified,

  // Second attempt to decode (decode what was just encoded and verified) succeeded.
  SecondDecodeSuccess,

  // Second attempt to encode (encode object resulting from `SecondDecodeSuccess` step) succeeded.
  SecondEncodeSuccess,
};

// DecoderEncoderStatus encapsulates data that results from attempting to decode and encode a
// particular a collection of bytes and handles as a particular FIDL type.
struct DecoderEncoderStatus {
  DecoderEncoderProgress progress;
  zx_status_t status;

  // First encoding data, relevant for `progress >= DecoderEncoderProgress::FirstEncodeSuccess`.
  std::vector<uint8_t> first_encoded_bytes;
  // TODO(fxbug.dev/72895): Add first handles for koid check.

  // Second encoding data, relevant for `progress >= DecoderEncoderProgress::SecondEncodeSuccess`.
  std::vector<uint8_t> second_encoded_bytes;
  // TODO(fxbug.dev/72895): Add second handles for koid check.
};

// An DecoderEncoder is a function that encapsulates the FIDL type-specific logic for attempting to
// decode and (if decode succeeds) re-encode a FIDL message via the interface documented at
// https://fuchsia.dev/fuchsia-src/reference/fidl/bindings/llcpp-bindings#encoding-decoding.
// Note that a function pointer is used instead of `::std::function` to facilitate header-only
// constexpr globals of type `::std::array<DecoderEncoder, n>`.
using DecoderEncoder = DecoderEncoderStatus (*)(uint8_t* bytes, uint32_t num_bytes,
                                                zx_handle_info_t* handles, uint32_t handle_actual);

}  // namespace fuzzing
}  // namespace fidl

#ifdef __cplusplus
}  // extern "C"
#endif

namespace fidl {
namespace fuzzing {

template <typename T>
DecoderEncoderStatus DecoderEncoderImpl(uint8_t* bytes, uint32_t num_bytes,
                                        zx_handle_info_t* handles, uint32_t num_handles) {
  DecoderEncoderStatus status = {
      .progress = DecoderEncoderProgress::NoProgress,
      .status = ZX_OK,
  };
  typename T::DecodedMessage decoded(bytes, num_bytes, handles, num_handles);

  if (decoded.status() != ZX_OK) {
    status.status = decoded.status();
    return status;
  }
  status.progress = DecoderEncoderProgress::FirstDecodeSuccess;

  T* value = decoded.PrimaryObject();
  typename T::OwnedEncodedMessage encoded(value);

  if (encoded.status() != ZX_OK) {
    status.status = encoded.status();
    return status;
  }
  status.progress = DecoderEncoderProgress::FirstEncodeSuccess;

  auto message_bytes = encoded.GetOutgoingMessage().CopyBytes();
  status.first_encoded_bytes =
      ::std::vector<uint8_t>(message_bytes.data(), message_bytes.data() + message_bytes.size());

  // TODO(fxbug.dev/72895): Add handles for koid check.

  auto conversion = ::fidl::OutgoingToIncomingMessage(encoded.GetOutgoingMessage());

  if (conversion.status() != ZX_OK) {
    status.status = encoded.status();
    return status;
  }
  status.progress = DecoderEncoderProgress::FirstEncodeVerified;

  typename T::DecodedMessage decoded2(conversion.incoming_message());

  if (decoded2.status() != ZX_OK) {
    status.status = decoded2.status();
    return status;
  }
  status.progress = DecoderEncoderProgress::SecondDecodeSuccess;

  T* value2 = decoded2.PrimaryObject();
  typename T::OwnedEncodedMessage encoded2(value2);

  if (encoded2.status() != ZX_OK) {
    status.status = encoded2.status();
    return status;
  }
  status.progress = DecoderEncoderProgress::SecondEncodeSuccess;

  auto message_bytes2 = encoded2.GetOutgoingMessage().CopyBytes();
  status.second_encoded_bytes =
      ::std::vector<uint8_t>(message_bytes2.data(), message_bytes2.data() + message_bytes2.size());

  // TODO(fxbug.dev/72895): Add handles for koid check.

  return status;
}

}  // namespace fuzzing
}  // namespace fidl

#endif  // LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_
