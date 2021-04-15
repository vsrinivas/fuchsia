// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_C_WALKER_TESTS_CONFORMANCE_TEST_UTILS_H_
#define SRC_LIB_FIDL_C_WALKER_TESTS_CONFORMANCE_TEST_UTILS_H_

#include <lib/fidl/coding.h>
#include <zircon/fidl.h>
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

namespace c_conformance_utils {

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

// Verifies that |value| encodes to |expected_bytes| and |expected_handles|.
// Note: This is destructive to |value| - a new value must be created with each call.
inline bool LinearizeAndEncodeSuccess(const fidl_type* type, void* value,
                                      const std::vector<uint8_t>& expected_bytes,
                                      const std::vector<zx_handle_t>& expected_handles) {
  alignas(FIDL_ALIGNMENT) uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes, actual_handles;
  const char* error_msg = nullptr;
  zx_status_t status = fidl_linearize_and_encode(type, value, bytes, ZX_CHANNEL_MAX_MSG_BYTES,
                                                 handles, ZX_CHANNEL_MAX_MSG_HANDLES, &actual_bytes,
                                                 &actual_handles, &error_msg);
  if (status != ZX_OK) {
    std::cout << "Encoding failed (" << zx_status_get_string(status) << "): " << error_msg
              << std::endl;
    return false;
  }
  if (error_msg != nullptr) {
    std::cout << "error message unexpectedly non-null when status is ZX_OK: " << error_msg
              << std::endl;
    return false;
  }

  bool bytes_match =
      ComparePayload(bytes, actual_bytes, expected_bytes.data(), expected_bytes.size());
  bool handles_match =
      ComparePayload(handles, actual_handles, expected_handles.data(), expected_handles.size());
  return bytes_match && handles_match;
}

// Verifies that |value| fails to encode and results in |expected_error_code|.
// Note: This is destructive to |value| - a new value must be created with each call.
inline bool LinearizeAndEncodeFailure(const fidl_type* type, void* value,
                                      zx_status_t expected_error_code) {
  alignas(FIDL_ALIGNMENT) uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes, actual_handles;
  const char* error_msg = nullptr;
  zx_status_t status = fidl_linearize_and_encode(type, value, bytes, ZX_CHANNEL_MAX_MSG_BYTES,
                                                 handles, ZX_CHANNEL_MAX_MSG_HANDLES, &actual_bytes,
                                                 &actual_handles, &error_msg);
  if (status == ZX_OK) {
    std::cout << "Encoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (status != expected_error_code) {
    std::cout << "Encoding failed with error code " << zx_status_get_string(status) << " ("
              << error_msg << "), but expected error code "
              << zx_status_get_string(expected_error_code) << std::endl;
    return false;
  }
  return true;
}

// Verifies that |bytes| and |handles| successfully decodes.
// TODO(fxbug.dev/67276) Check deep equality of decoded value.
inline bool DecodeSuccess(const fidl_type* type, std::vector<uint8_t> bytes,
                          std::vector<zx_handle_t> handles) {
  const char* error_msg = nullptr;
  zx_status_t status =
      fidl_decode(type, bytes.data(), bytes.size(), handles.data(), handles.size(), &error_msg);
  if (status != ZX_OK) {
    std::cout << "Decoding failed (" << zx_status_get_string(status) << "): " << error_msg
              << std::endl;
    return false;
  }
  if (error_msg != nullptr) {
    std::cout << "error message unexpectedly non-null when status is ZX_OK: " << error_msg
              << std::endl;
    return false;
  }
  return true;
}

// Verifies that |bytes| and |handles| fails to decode.
inline bool DecodeFailure(const fidl_type* type, std::vector<uint8_t> bytes,
                          std::vector<zx_handle_t> handles, zx_status_t expected_error_code) {
  const char* error_msg = nullptr;
  zx_status_t status =
      fidl_decode(type, bytes.data(), bytes.size(), handles.data(), handles.size(), &error_msg);
  if (status == ZX_OK) {
    std::cout << "Decoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (status != expected_error_code) {
    std::cout << "Decoding failed with error code " << zx_status_get_string(status) << " ("
              << error_msg << "), but expected error code "
              << zx_status_get_string(expected_error_code) << std::endl;
    return false;
  }
  return true;
}

}  // namespace c_conformance_utils

#endif  // SRC_LIB_FIDL_C_WALKER_TESTS_CONFORMANCE_TEST_UTILS_H_
