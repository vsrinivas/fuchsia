// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is separate from test_utils as it is not used in conformance tests and
// can therefore e.g. use handles

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TYPES_TEST_UTILS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TYPES_TEST_UTILS_H_

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/linearized_and_encoded.h>
#include <lib/zx/event.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <vector>

#include <gtest/gtest.h>

namespace llcpp_types_test_utils {

class HandleChecker {
 public:
  HandleChecker() = default;

  size_t size() const { return events_.size(); }

  void AddEvent(zx_handle_t event);
  void CheckEvents();

 private:
  std::vector<zx::event> events_;
};

// Verifies that:
//   - |bytes| and |handles| decodes succesfully as |FidlType|
//   - all handles in |handles| are closed
//   - the resulting object fails to encode
// Assuming that:
//   - FidlType is a transactional message, with a single result field that is
//     either a union or a table.
//
// Also runs a checker function on the decoded object, to test any properties.
// This is the intended behavior for all flexible types (unions and tables) in
// LLCPP, regardless of resourceness (since no unknown handles are stored, even on
// resource types).
template <typename FidlType, typename CheckerFunc>
void CannotProxyUnknownEnvelope(std::vector<uint8_t> bytes, std::vector<zx_handle_t> handles,
                                CheckerFunc check) {
  // These flexible tests require the type be a message so that traits like
  // HasFlexibleEnvelope exist.
  // It also assumes the type under test exists under field .result within the
  // wrapper struct
  static_assert(fidl::IsFidlMessage<FidlType>::value, "FIDL message type required");

  HandleChecker handle_checker;
  for (const auto& handle : handles) {
    handle_checker.AddEvent(handle);
  }

  const char* decode_error;
  auto status = fidl_decode(FidlType::Type, bytes.data(), bytes.size(), handles.data(),
                            handles.size(), &decode_error);
  ASSERT_EQ(status, ZX_OK) << decode_error;

  auto result = reinterpret_cast<FidlType*>(&bytes[0]);
  check(result->result);
  handle_checker.CheckEvents();

  fidl::internal::LinearizeBuffer<FidlType> buffer;
  auto encode_result = fidl::LinearizeAndEncode(result, buffer.buffer());
  ASSERT_EQ(encode_result.status, ZX_ERR_INVALID_ARGS)
      << zx_status_get_string(encode_result.status);

  EXPECT_STREQ(encode_result.error, "Cannot encode unknown union or table") << encode_result.error;
}

}  // namespace llcpp_types_test_utils

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_TYPES_TEST_UTILS_H_
