// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/linearized.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

// Testing utilities indended for GIDL-generated conformance tests.
namespace llcpp_conformance_utils {

bool ComparePayload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                    size_t expected_size);

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool LinearizeThenEncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = std::move(*value);
  auto linearized = fidl::internal::Linearized<FidlType>(&aligned_value.value);
  auto& linearize_result = linearized.result();
  if (linearize_result.status != ZX_OK || linearize_result.error != nullptr) {
    std::cout << "Linearization failed (" << zx_status_get_string(linearize_result.status)
              << "): " << linearize_result.error << std::endl;
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

// Verifies that |value| fails to encode, with the expected error code.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool LinearizeThenEncodeFailure(FidlType* value, zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = std::move(*value);
  auto linearized = fidl::internal::Linearized<FidlType>(&aligned_value.value);
  auto& linearize_result = linearized.result();
  if (linearize_result.status != ZX_OK && linearize_result.status != expected_error_code) {
    std::cout << "Linearization failed with error code "
              << zx_status_get_string(linearize_result.status) << " (" << linearize_result.error
              << "), but expected error code " << zx_status_get_string(expected_error_code)
              << std::endl;
    return false;
  }

  auto encode_result = fidl::Encode(std::move(linearize_result.message));
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

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool CombinedLinearizeAndEncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
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
bool CombinedLinearizeAndEncodeFailure(FidlType* value, zx_status_t expected_error_code) {
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
  fidl::EncodedMessage<FidlType> message(fidl::BytePart(&bytes[0], bytes.size(), bytes.size()));
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
  fidl::EncodedMessage<FidlType> message(fidl::BytePart(&bytes[0], bytes.size(), bytes.size()));
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
