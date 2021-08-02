// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_TRANSFORMER_TESTS_CONFORMANCE_UTIL_H_
#define SRC_LIB_FIDL_TRANSFORMER_TESTS_CONFORMANCE_UTIL_H_

#include <lib/fidl/transformer.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace transformer_conformance_utils {

template <typename FidlType>
void FidlTransformSuccessCase(fidl_transformation_t transformation,
                              const std::vector<uint8_t>& input_bytes,
                              const std::vector<uint8_t>& expected_bytes) {
  auto buffer_bytes = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t bytes_actual;
  const char* error = nullptr;
  zx_status_t status = internal__fidl_transform__may_break(
      transformation, FidlType::Type, input_bytes.data(), input_bytes.size(), buffer_bytes.get(),
      ZX_CHANNEL_MAX_MSG_BYTES, &bytes_actual, &error);
  ASSERT_OK(status);
  ASSERT_NULL(error);
  ASSERT_EQ(expected_bytes.size(), bytes_actual);
  ASSERT_BYTES_EQ(expected_bytes.data(), buffer_bytes.get(), bytes_actual);
}

// Just run the transform to ensure it doesn't crash. Don't worry about the output.
template <typename FidlType>
void FidlTransformFailureCase(fidl_transformation_t transformation,
                              const std::vector<uint8_t>& input_bytes) {
  auto buffer_bytes = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t bytes_actual;
  const char* error = nullptr;
  internal__fidl_transform__may_break(transformation, FidlType::Type, input_bytes.data(),
                                      input_bytes.size(), buffer_bytes.get(),
                                      ZX_CHANNEL_MAX_MSG_BYTES, &bytes_actual, &error);
}
}  // namespace transformer_conformance_utils

#endif  // SRC_LIB_FIDL_TRANSFORMER_TESTS_CONFORMANCE_UTIL_H_
