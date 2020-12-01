// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TEST_UTILS_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/traits.h>
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

// TODO(fxbug.dev/63900): Remove this when rights are specified in GIDL.
std::vector<zx_handle_info_t> ToHandleInfoVec(std::vector<zx_handle_t> handles);

template <typename T>
bool ComparePayload(const T* actual, size_t actual_size, const T* expected, size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      std::cout << std::dec << "element[" << i << "]: " << std::hex << "actual=0x" << +actual[i]
                << " "
                << "expected=0x" << +expected[i] << "\n";
    }
  }
  if (actual_size != expected_size) {
    pass = false;
    std::cout << std::dec << "element[...]: "
              << "actual.size=" << +actual_size << " "
              << "expected.size=" << +expected_size << "\n";
  }
  return pass;
}

// Verifies that |value| encodes to |bytes|.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool EncodeSuccess(FidlType* value, const std::vector<uint8_t>& bytes,
                   const std::vector<zx_handle_t>& handles) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::OwnedEncodedMessage<FidlType> encoded(value);
  if (!encoded.ok() || encoded.error() != nullptr) {
    std::cout << "Encoding failed (" << zx_status_get_string(encoded.status())
              << "): " << encoded.error() << std::endl;
    return false;
  }
  bool bytes_match =
      ComparePayload(encoded.GetOutgoingMessage().bytes(),
                     encoded.GetOutgoingMessage().byte_actual(), bytes.data(), bytes.size());
  std::vector<zx_handle_t> outgoing_msg_handles;
  for (size_t i = 0; i < encoded.GetOutgoingMessage().handle_actual(); i++) {
    outgoing_msg_handles.push_back(encoded.GetOutgoingMessage().handles()[i].handle);
  }
  bool handles_match = ComparePayload(outgoing_msg_handles.data(), outgoing_msg_handles.size(),
                                      handles.data(), handles.size());
  return bytes_match && handles_match;
}

// Verifies that |value| fails to encode, with the expected error code.
// Note: This is destructive to |value| - a new value must be created with each call.
template <typename FidlType>
bool EncodeFailure(FidlType* value, zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  ::fidl::OwnedEncodedMessage<FidlType> encoded(value);
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
bool DecodeSuccess(FidlType* value, std::vector<uint8_t> bytes, std::vector<zx_handle_t> handles) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");
  auto handle_infos = ToHandleInfoVec(std::move(handles));
  fidl::DecodedMessage<FidlType> decoded(bytes.data(), static_cast<uint32_t>(bytes.size()),
                                         handle_infos.data(),
                                         static_cast<uint32_t>(handle_infos.size()));
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
bool DecodeFailure(std::vector<uint8_t> bytes, std::vector<zx_handle_t> handles,
                   zx_status_t expected_error_code) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");
  auto handle_infos = ToHandleInfoVec(std::move(handles));
  fidl::DecodedMessage<FidlType> decoded(bytes.data(), static_cast<uint32_t>(bytes.size()),
                                         handle_infos.data(),
                                         static_cast<uint32_t>(handle_infos.size()));
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
