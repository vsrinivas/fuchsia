// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/linearized_and_encoded.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#ifndef __Fuchsia__
// The current build rules for zircon/system/ulib/zircon don't allow linking
// zx_status_get_string on host. Consider changing in the future.
#define zx_status_get_string(status) ((status))
#endif

// Testing utilities indended for GIDL-generated conformance tests.
namespace llcpp_conformance_utils {

bool ComparePayload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                    size_t expected_size);

template <typename FidlType>
struct LinearizedResult {
  ::fidl::internal::LinearizeBuffer<FidlType> buffer;
  ::fidl::DecodedMessage<FidlType> message;
  zx_status_t status;
};

// Linearizes by encoding and then decoding into a linearized form.
// fidl::Linearize is being removed in favor of fidl::LinearizeAndEncode,
// so it is no longer possible to directly linearize arbitrary values.
template <typename FidlType>
LinearizedResult<FidlType> LinearizeForTest(FidlType* value) {
  LinearizedResult<FidlType> result;
  auto encode_result = fidl::LinearizeAndEncode(value, result.buffer.buffer());
  if (encode_result.status != ZX_OK || encode_result.error != nullptr) {
    result.status = encode_result.status;
    std::cout << "LinearizeAndEncode in LinearizeForTest failed ("
              << zx_status_get_string(encode_result.status) << ")" << encode_result.error
              << std::endl;
    return result;
  }
  auto decode_result = fidl::Decode(std::move(encode_result.message));
  if (decode_result.status != ZX_OK || decode_result.error != nullptr) {
    result.status = decode_result.status;
    std::cout << "Decode in LinearizeForTest failed (" << zx_status_get_string(decode_result.status)
              << ")" << decode_result.error << std::endl;
    return result;
  }
  result.status = ZX_OK;
  result.message = std::move(decode_result.message);
  return result;
}

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool EncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = std::move(*value);
  auto linearize_result = LinearizeForTest<FidlType>(&aligned_value.value);
  if (linearize_result.status != ZX_OK) {
    return false;
  }

  auto encode_result = fidl::Encode(std::move(linearize_result.message));
  if (encode_result.status != ZX_OK || encode_result.error != nullptr) {
    std::cout << "Encoding failed (" << zx_status_get_string(encode_result.status)
              << "): " << encode_result.error << std::endl;
    return false;
  }
  return ComparePayload(encode_result.message.bytes().data(),
                        encode_result.message.bytes().actual(), &bytes[0], bytes.size());
}

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool LinearizeAndEncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::internal::LinearizeBuffer<FidlType> buffer;
  auto encode_result = fidl::LinearizeAndEncode(value, buffer.buffer());
  if (encode_result.status != ZX_OK || encode_result.error != nullptr) {
    std::cout << "Encoding failed (" << zx_status_get_string(encode_result.status)
              << "): " << encode_result.error << std::endl;
    return false;
  }
  return ComparePayload(encode_result.message.bytes().data(),
                        encode_result.message.bytes().actual(), &bytes[0], bytes.size());
}

// Verifies that |value| fails to encode, with the expected error code.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool LinearizeAndEncodeFailure(FidlType* value, zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::internal::LinearizeBuffer<FidlType> buffer;
  auto encode_result = fidl::LinearizeAndEncode(value, buffer.buffer());
  if (encode_result.status == ZX_OK) {
    std::cout << "Encoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (encode_result.status != expected_error_code) {
    std::cout << "Encoding failed with error code " << zx_status_get_string(encode_result.status)
              << " (" << encode_result.error << "), but expected error code "
              << zx_status_get_string(expected_error_code) << std::endl;
    return false;
  }
  return true;
}

// Verifies that |bytes| decodes to an object that is the same as |value|.
template <typename FidlType>
bool DecodeSuccess(FidlType* value, std::vector<uint8_t> bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");
  uint32_t size = static_cast<uint32_t>(bytes.size());
  fidl::EncodedMessage<FidlType> message(fidl::BytePart(&bytes[0], size, size));
  auto decode_result = fidl::Decode(std::move(message));
  if (decode_result.status != ZX_OK || decode_result.error != nullptr) {
    std::cout << "Decoding failed (" << zx_status_get_string(decode_result.status)
              << "): " << decode_result.error << std::endl;
    return false;
  }
  // TODO(fxb/7958): For now we are only checking that fidl::Decode succeeds.
  // We need deep equality on FIDL objects to verify that
  // |decode_result.message| is the same as |value|.
  return true;
}

// Verifies that |bytes| fails to decode as |FidlType|, with the expected error
// code.
template <typename FidlType>
bool DecodeFailure(std::vector<uint8_t> bytes, zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");
  uint32_t size = static_cast<uint32_t>(bytes.size());
  fidl::EncodedMessage<FidlType> message(fidl::BytePart(&bytes[0], size, size));
  auto decode_result = fidl::Decode(std::move(message));
  if (decode_result.status == ZX_OK) {
    std::cout << "Decoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (decode_result.status != expected_error_code) {
    std::cout << "Decoding failed with error code " << zx_status_get_string(decode_result.status)
              << " (" << decode_result.error << "), but expected error code "
              << zx_status_get_string(expected_error_code) << std::endl;
    return false;
  }
  return true;
}

constexpr inline uint64_t FidlAlign(uint32_t offset) {
  constexpr uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (offset + alignment_mask) & ~alignment_mask;
}

}  // namespace llcpp_conformance_utils

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_
