// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
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

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool EncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::OwnedOutgoingMessage<FidlType> encoded(value);
  if (!encoded.ok() || encoded.error() != nullptr) {
    std::cout << "Encoding failed (" << zx_status_get_string(encoded.status())
              << "): " << encoded.error() << std::endl;
    return false;
  }
  return ComparePayload(encoded.GetOutgoingMessage().bytes(),
                        encoded.GetOutgoingMessage().byte_actual(), &bytes[0], bytes.size());
}

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool LinearizeAndEncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::OwnedOutgoingMessage<FidlType> encoded(value);
  if (!encoded.ok() || encoded.error() != nullptr) {
    std::cout << "Encoding failed (" << zx_status_get_string(encoded.status())
              << "): " << encoded.error() << std::endl;
    return false;
  }
  return ComparePayload(encoded.GetOutgoingMessage().bytes(),
                        encoded.GetOutgoingMessage().byte_actual(), &bytes[0], bytes.size());
}

// Verifies that |value| fails to encode, with the expected error code.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool LinearizeAndEncodeFailure(FidlType* value, zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::OwnedOutgoingMessage<FidlType> encoded(value);
  if (encoded.ok()) {
    std::cout << "Encoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (encoded.status() != expected_error_code) {
    std::cout << "Encoding failed with error code " << zx_status_get_string(encoded.status())
              << " (" << encoded.error() << "), but expected error code "
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
  fidl::IncomingMessage<FidlType> decoded(bytes.data(), size, nullptr, 0);
  if (!decoded.ok() || decoded.error() != nullptr) {
    std::cout << "Decoding failed (" << zx_status_get_string(decoded.status())
              << "): " << decoded.error() << std::endl;
    return false;
  }
  // TODO(fxbug.dev/7958): For now we are only checking that fidl::Decode succeeds.
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
  fidl::IncomingMessage<FidlType> decoded(bytes.data(), size, nullptr, 0);
  if (decoded.ok()) {
    std::cout << "Decoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (decoded.status() != expected_error_code) {
    std::cout << "Decoding failed with error code " << zx_status_get_string(decoded.status())
              << " (" << decoded.error() << "), but expected error code "
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
