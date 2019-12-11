// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TEST_UTILS_H_
#define LIB_FIDL_LLCPP_TEST_UTILS_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace llcpp_conformance_utils {

bool ComparePayload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                    size_t expected_size);

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value|.
template <typename FidlType>
bool EncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  FIDL_ALIGNDECL char aligned_value_buf[FidlAlign(sizeof(FidlType))] = {};
  FidlType* aligned_value = new (aligned_value_buf) FidlType(std::move(*value));

  fidl::DecodedMessage<FidlType> message;
  std::vector<uint8_t> buffer(ZX_CHANNEL_MAX_MSG_BYTES);
  if constexpr (FidlType::Type != nullptr && FidlType::MaxOutOfLine > 0) {
    auto linearize_result =
        fidl::Linearize(aligned_value, fidl::BytePart(&buffer[0], ZX_CHANNEL_MAX_MSG_BYTES));
    if (linearize_result.status != ZX_OK || linearize_result.error != nullptr) {
      std::cout << "Linearization failed (" << zx_status_get_string(linearize_result.status)
                << "): " << linearize_result.error << std::endl;
      return false;
    }
    message = std::move(linearize_result.message);
  } else {
    message = fidl::DecodedMessage<FidlType>(
        fidl::BytePart(reinterpret_cast<uint8_t*>(aligned_value), FidlAlign(sizeof(FidlType)),
                       FidlAlign(sizeof(FidlType))));
  }

  auto encode_result = fidl::Encode(std::move(message));
  if (encode_result.status != ZX_OK || encode_result.error != nullptr) {
    std::cout << "Encoding failed (" << zx_status_get_string(encode_result.status)
              << "): " << encode_result.error << std::endl;
    return false;
  }
  return ComparePayload(encode_result.message.bytes().data(),
                        encode_result.message.bytes().actual(), &bytes[0], bytes.size());
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

constexpr inline uint64_t FidlAlign(uint32_t offset) {
  constexpr uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (offset + alignment_mask) & ~alignment_mask;
}

}  // namespace llcpp_conformance_utils

#endif  // LIB_FIDL_LLCPP_TEST_UTILS_H_
