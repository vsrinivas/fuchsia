// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TEST_UTILS_H_
#define LIB_FIDL_LLCPP_TEST_UTILS_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace llcpp_conformance_utils {

bool ComparePayload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                    size_t expected_size);

// A wrapper around fidl::LinearizeResult that owns the memory.
template <typename FidlType>
struct OwnedLinearizeResult {
  // The result, whose message field points to either inline_buffer or
  // linearized_buffer (or neither in error conditions).
  fidl::LinearizeResult<FidlType> result;
  // Used for types with no out-of-line parts, which do not need linearization.
  FIDL_ALIGNDECL char inline_buffer[FidlAlign(sizeof(FidlType))] = {};
  // Used in the general case where linearization is needed.
  std::vector<uint8_t> linearized_buffer;
};

// Linearizes |value|, producing an |OwnedLinearizeResult|.
// Note: This is destructive to |value|.
template <typename FidlType>
OwnedLinearizeResult<FidlType> Linearize(FidlType* value) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  OwnedLinearizeResult<FidlType> owned_result;
  FidlType* aligned_value = new (owned_result.inline_buffer) FidlType(std::move(*value));

  if constexpr (FidlType::HasPointer) {
    owned_result.linearized_buffer.resize(ZX_CHANNEL_MAX_MSG_BYTES);
    owned_result.result = fidl::Linearize(
        aligned_value,
        fidl::BytePart(&owned_result.linearized_buffer[0], ZX_CHANNEL_MAX_MSG_BYTES));
  } else {
    owned_result.result =
        fidl::LinearizeResult(ZX_OK, nullptr,
                              fidl::DecodedMessage<FidlType>(fidl::BytePart(
                                  reinterpret_cast<uint8_t*>(aligned_value),
                                  FidlAlign(sizeof(FidlType)), FidlAlign(sizeof(FidlType)))));
  }
  return owned_result;
}

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value|.
template <typename FidlType>
bool EncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  auto owned_linearize_result = Linearize(value);
  auto& linearize_result = owned_linearize_result.result;
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
// Note: This is destructive to |value|.
template <typename FidlType>
bool EncodeFailure(FidlType* value, zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  auto owned_linearize_result = Linearize(value);
  auto& linearize_result = owned_linearize_result.result;
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

#endif  // LIB_FIDL_LLCPP_TEST_UTILS_H_
