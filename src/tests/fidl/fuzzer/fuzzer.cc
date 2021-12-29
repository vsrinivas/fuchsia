// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <iomanip>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>
#include <test/conformance/cpp/libfuzzer_decode_encode.h>

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

using DecoderEncoderForType = ::fidl::fuzzing::DecoderEncoderForType;
using DecoderEncoderProgress = ::fidl::fuzzing::DecoderEncoderProgress;
using DecoderEncoderStatus = ::fidl::fuzzing::DecoderEncoderStatus;

class DecoderEncoderInput {
 public:
  DecoderEncoderInput(const uint8_t* const bytes, const size_t size) : bytes_(bytes), size_(size) {}
  const uint8_t* data() const { return bytes_; }
  size_t size() const { return size_; }

 private:
  const uint8_t* const bytes_;
  const size_t size_;
};

// Prints the contents of `first` to `stderr`, highlighting the bytes that differ from `second`.
template <typename T1, typename T2>
void ReportFirstByteArray(const T1& first, const char* const first_label, const T2& second,
                          const char* const second_label) {
  std::cerr << std::endl << first_label << " (diff'd against " << second_label << "):" << std::endl;
  if (first.size() == 0) {
    std::cerr << "<empty byte array>";
  } else {
    for (size_t i = 0; i < first.size(); i++) {
      if (i != 0 && i % 4 == 0)
        std::cerr << std::endl;

      const uint8_t* const first_data = first.data();
      const uint8_t* const second_data = second.data();
      const uint32_t uint_value = first_data[i];
      const bool is_diff = i >= second.size() || first_data[i] != second_data[i];

      std::cerr << (is_diff ? "[" : " ");
      std::cerr << "0x" << std::hex << std::setfill('0') << std::setw(2) << uint_value;
      std::cerr << (is_diff ? "]" : " ");
    }
  }

  std::cerr << std::endl;
}

#define REPORT_BYTE_ARRAY_DIFF(first, second)             \
  {                                                       \
    ReportFirstByteArray(first, #first, second, #second); \
    ReportFirstByteArray(second, #second, first, #first); \
  }

void ReportTestCase(const DecoderEncoderInput& input,
                    const DecoderEncoderForType& decoder_encoder_for_type,
                    const DecoderEncoderStatus& status) {
  std::cerr << std::endl
            << "FIDL wire type:" << std::endl
            << decoder_encoder_for_type.fidl_type_name << std::endl
            << "flexible envelope? " << std::boolalpha
            << decoder_encoder_for_type.has_flexible_envelope << std::endl;
  std::cerr << std::endl << "Decode/encode progress:" << std::endl << status.progress << std::endl;
  std::cerr << std::endl << "Decode/encode status:" << std::endl << status.status << std::endl;

  const std::vector<uint8_t>& first_encoded_bytes = status.first_encoded_bytes;
  REPORT_BYTE_ARRAY_DIFF(input, first_encoded_bytes);
  const std::vector<uint8_t>& second_encoded_bytes = status.second_encoded_bytes;
  REPORT_BYTE_ARRAY_DIFF(first_encoded_bytes, second_encoded_bytes);

  // TODO(fxbug.dev/72895): Report second handle data.
}

#define ASSERT_TEST_CASE(cond, input, decoder_encoder_for_type, status)  \
  {                                                                      \
    if (!(cond)) {                                                       \
      std::cerr << "TEST CASE ASSERTION FAILED: " << #cond << std::endl; \
      ReportTestCase(input, decoder_encoder_for_type, status);           \
    }                                                                    \
    assert(cond);                                                        \
  }

// If decoder/encoder progressed to a second round-trip, then check that it completed the round-trip
// successfully, and the re-encoded data from both round-trips match.
void CheckDecoderEncoderDoubleRoundTrip(const DecoderEncoderInput& input,
                                        const DecoderEncoderForType& decoder_encoder_for_type,
                                        const DecoderEncoderStatus& status) {
  // No symmetry verification unless first decode/encode round-trip succeeded and verified. This is
  // because unexpected data in a flexible envelope may be accepted on decode, but invalid to
  // re-encode.
  if (status.progress < DecoderEncoderProgress::FirstEncodeVerified)
    return;

    // If no early return above, then second decode-encode round-trip should have succeeded and data
    // should match.
#define ASSERT_LOCAL(cond) ASSERT_TEST_CASE(cond, input, decoder_encoder_for_type, status)
  ASSERT_LOCAL(status.progress >= DecoderEncoderProgress::SecondEncodeSuccess);
  ASSERT_LOCAL(status.first_encoded_bytes.size() == status.second_encoded_bytes.size());
  ASSERT_LOCAL(memcmp(status.first_encoded_bytes.data(), status.second_encoded_bytes.data(),
                      status.first_encoded_bytes.size()) == 0);
#undef ASSERT_LOCAL

  // TODO(fxbug.dev/72895): Check handle koids.
}

// If initial decoding succeeded, then check that a decode/encode round-trip succeeded, and
// re-encoded the same data.
void CheckDecoderEncoderRoundTrip(const DecoderEncoderInput& input,
                                  const DecoderEncoderForType& decoder_encoder_for_type,
                                  const DecoderEncoderStatus& status) {
  // No symmetry verification unless initial decode succeeded.
  if (status.progress < DecoderEncoderProgress::FirstDecodeSuccess)
    return;

    // If no early return above, then first decode-encode round-trip should have succeeded and
    // verified, and data should match.
#define ASSERT_LOCAL(cond) ASSERT_TEST_CASE(cond, input, decoder_encoder_for_type, status)
  ASSERT_LOCAL(status.progress >= DecoderEncoderProgress::FirstEncodeVerified);
  ASSERT_LOCAL(input.size() == status.first_encoded_bytes.size());
  ASSERT_LOCAL(memcmp(input.data(), status.first_encoded_bytes.data(), input.size()) == 0);
#undef ASSERT_LOCAL

  // TODO(fxbug.dev/72895): Check handle koids.
}

#undef ASSERT_TEST_CASE

void CheckDecoderEncoderResult(const DecoderEncoderInput& input,
                               const DecoderEncoderForType& decoder_encoder_for_type,
                               const DecoderEncoderStatus& status) {
  if (decoder_encoder_for_type.has_flexible_envelope) {
    // Data with flexible envelopes can only perform symmetry checks on a "double round-trip"
    // because unexpected data in a flexible envelope may be accepted on decode, but invalid to
    // re-encode. Only after the re-encode succeeds and is verified can a symmetry check on a second
    // round-trip be performed (i.e., ensure both re-encodings match).
    CheckDecoderEncoderDoubleRoundTrip(input, decoder_encoder_for_type, status);
  } else {
    // No flexible envelope: Just check check single round-trip: successful decode implies
    // successful re-encode of the same data.
    CheckDecoderEncoderRoundTrip(input, decoder_encoder_for_type, status);
  }
}

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

  std::vector<zx_handle_t> handles;
  std::vector<fidl_channel_handle_metadata_t> handle_metadata;
  for (uint64_t i = 0; i < num_handles; i++) {
    zx_obj_type_t obj_type;

    // Consume data: Handles.
    // Note: Data (non-length-encodings) drawn from head.
    // 1. Handle type.
    FirstAs(&remaining_data, &remaining_size, &obj_type);
    // TODO(markdittmer): Use interesting handle rights and values. This may require a change in
    // corpus data format.
    handles.push_back(ZX_HANDLE_INVALID);
    handle_metadata.push_back(fidl_channel_handle_metadata_t{
        .obj_type = obj_type,
        .rights = 0,
    });
  }

  // Remaining data goes into `message`, and `message.size()` later cast as `uint32_t`.
  if (remaining_size > std::numeric_limits<uint32_t>::max())
    return 0;

  const DecoderEncoderInput decoder_encoder_input(remaining_data, remaining_size);

  for (auto decoder_encoder_for_type : fuzzing::test_conformance_decoder_encoders) {
    // Decode/encode require non-const data pointer: Copy remaining data into (non-const) vector.
    std::vector<uint8_t> message(decoder_encoder_input.data(),
                                 decoder_encoder_input.data() + decoder_encoder_input.size());

    // Result is unused on builds with assertions disabled.
    [[maybe_unused]] auto decode_encode_status = decoder_encoder_for_type.decoder_encoder(
        message.data(), static_cast<uint32_t>(message.size()), handles.data(),
        handle_metadata.data(), static_cast<uint32_t>(handles.size()));

    CheckDecoderEncoderResult(decoder_encoder_input, decoder_encoder_for_type,
                              decode_encode_status);
  }

  return 0;
}
