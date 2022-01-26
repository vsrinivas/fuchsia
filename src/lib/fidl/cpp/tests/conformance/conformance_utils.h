// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_
#define SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_

#include <lib/fidl/cpp/internal/codable_base.h>
#include <lib/fidl/internal.h>

#include <zxtest/zxtest.h>

namespace conformance_utils {

template <typename FidlType>
void EncodeSuccess(fidl::internal::WireFormatVersion wire_format_version, FidlType& obj,
                   const std::vector<uint8_t>& bytes) {
  fidl::internal::EncodeResult result = obj.Internal__Encode();
  ASSERT_TRUE(result.message().ok(), "Error encoding: %s",
              result.message().error().FormatDescription().c_str());

  auto result_bytes = result.message().CopyBytes();
  ASSERT_EQ(bytes.size(), result_bytes.size());
  ASSERT_BYTES_EQ(bytes.data(), result_bytes.data(), bytes.size());
}

}  // namespace conformance_utils

#endif  // SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_
