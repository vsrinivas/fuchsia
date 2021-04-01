// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <limits>
#include <utility>
#include <vector>

#include <conformance/cpp/libfuzzer_decode_encode.h>
#include <fuzzer/FuzzedDataProvider.h>

namespace {

// Caller must ensure that `*size >= sizeof(T)`. Using `out` parameter ensures match with implicit
// template specialization.
template <typename T>
void FirstAs(const uint8_t** data, size_t* size, T* out) {
  assert(*size >= sizeof(T));

  // Use byte-by-byte copy strategy to avoid "load of misaligned address".
  uint8_t* out_bytes = (uint8_t*)(out);
  memcpy(out_bytes, *data, sizeof(T));

  *data += sizeof(T);
  *size -= sizeof(T);
}

// Caller must ensure that `*size >= sizeof(T)`. Using `out` parameter ensures match with implicit
// template specialization.
template <typename T>
void LastAs(const uint8_t** data, size_t* size, T* out) {
  assert(*size >= sizeof(T));

  // Use byte-by-byte copy strategy to avoid "load of misaligned address".
  uint8_t* out_bytes = (uint8_t*)(out);
  memcpy(out_bytes, *data + *size - sizeof(T), sizeof(T));

  *size -= sizeof(T);
}

constexpr uint64_t kMaxHandles = 2 * ZX_CHANNEL_MAX_MSG_HANDLES;

using DecoderEncoderProgress = ::fidl::fuzzing::DecoderEncoderProgress;

}  // namespace

// This assertion guards `static_cast<uint32_t>(handle_infos.size())` below, where
// `handle_infos.size() = num_handles` and `num_handles <= kMaxHandles`.
static_assert(kMaxHandles <= std::numeric_limits<uint32_t>::max());

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const uint8_t* remaining_data = data;
  size_t remaining_size = size;

  // Follow libfuzzer best practice: Length encodings drawn from tail.
  uint64_t num_handles;
  if (remaining_size < sizeof(num_handles))
    return 0;
  LastAs(&remaining_data, &remaining_size, &num_handles);
  // Test oversized, but not ludicrously sized, collection of handles.
  num_handles %= kMaxHandles + 1;

  // Size check: Handles.
  // 1. Handle type.
  //
  // TODO(markdittmer): Use interesting handle rights and values. This may require a change in
  // corpus data format.
  if (remaining_size < num_handles * sizeof(zx_obj_type_t))
    return 0;

  std::vector<zx_handle_info_t> handle_infos;
  for (uint64_t i = 0; i < num_handles; i++) {
    zx_handle_info_t handle_info;

    // Consume data: Handles.
    // Note: Data (non-length-encodings) drawn from head.
    // 1. Handle type.
    FirstAs(&remaining_data, &remaining_size, &handle_info.type);
    // TODO(markdittmer): Use interesting handle rights and values. This may require a change in
    // corpus data format.
    handle_info.rights = 0;
    handle_info.handle = ZX_HANDLE_INVALID;

    handle_infos.push_back(handle_info);
  }

  // Remaining data goes into `message`, and `message.size()` later cast as `uint32_t`.
  if (remaining_size > std::numeric_limits<uint32_t>::max())
    return 0;

  const uint8_t* const message_data = remaining_data;
  const size_t message_size = remaining_size;

  for (auto decoder_encoder : fuzzing::conformance_decoder_encoders) {
    // Decode/encode require non-const data pointer: Copy remaining data into (non-const) vector.
    std::vector<uint8_t> message(message_data, message_data + message_size);

    // Result is unused on builds with assertions disabled.
    [[maybe_unused]] auto decode_encode_status =
        decoder_encoder(message.data(), static_cast<uint32_t>(message.size()), handle_infos.data(),
                        static_cast<uint32_t>(handle_infos.size()));

    // First encode successfully verified implies second encode success.
    assert(decode_encode_status.progress < DecoderEncoderProgress::FirstEncodeVerified ||
           decode_encode_status.progress >= DecoderEncoderProgress::SecondEncodeSuccess);

    // TODO(fxbug.dev/72895): When type contains no flexible envelopes, first decode implies first
    // encode.

    // If encode/decode round trip completed, check resulting data.
    if (decode_encode_status.progress >= DecoderEncoderProgress::SecondEncodeSuccess) {
      assert(decode_encode_status.first_encoded_bytes.size() ==
             decode_encode_status.second_encoded_bytes.size());
      assert(memcmp(decode_encode_status.first_encoded_bytes.data(),
                    decode_encode_status.second_encoded_bytes.data(),
                    decode_encode_status.first_encoded_bytes.size()) == 0);

      // TODO(fxbug.dev/72895): Check handle koids.
    }
  }

  return 0;
}
