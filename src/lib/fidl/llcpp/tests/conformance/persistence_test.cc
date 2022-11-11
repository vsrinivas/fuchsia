// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/wire_types.h>
#include <lib/zx/event.h>
#include <zircon/fidl.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

static std::vector<uint8_t> GetWireStructBytes() {
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Wire format metadata.
      0, kFidlWireFormatMagicNumberInitial, FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0,
      0, 0, 0, 0,
      // Payload, a single uint32_t.
      42, 0, 0, 0, 0, 0, 0, 0,
  };
  // clang-format on
  return bytes;
}

TEST(Unpersist, TooFewBytesError) {
  std::vector<uint8_t> bytes = {1, 2, 3};
  fit::result result =
      ::fidl::InplaceUnpersist<test_types::wire::CopyableStruct>(cpp20::span(bytes));
  ASSERT_FALSE(result.is_ok());
  ASSERT_EQ(fidl::Reason::kDecodeError, result.error_value().reason());
}

TEST(Unpersist, TooManyBytesError) {
  for (size_t n : {1, 8}) {
    std::vector<uint8_t> bytes = GetWireStructBytes();
    for (size_t i = 0; i < n; i++) {
      bytes.push_back(0);
    }
    fit::result result =
        ::fidl::InplaceUnpersist<test_types::wire::CopyableStruct>(cpp20::span(bytes));
    ASSERT_FALSE(result.is_ok()) << "Should fail with " << n << " extra bytes";
    ASSERT_EQ(fidl::Reason::kDecodeError, result.error_value().reason());
  }
}

TEST(Unpersist, WireStruct) {
  std::vector<uint8_t> bytes = GetWireStructBytes();
  EXPECT_EQ(bytes.size(), 16U);

  fit::result result =
      ::fidl::InplaceUnpersist<test_types::wire::CopyableStruct>(cpp20::span(bytes));
  ASSERT_TRUE(result.is_ok()) << "Error during unpersist: " << result.error_value();
  test_types::wire::CopyableStruct& obj = *result.value();

  // Check decoded value.
  EXPECT_EQ(42, obj.x);
}

TEST(Persist, WireStruct) {
  test_types::wire::CopyableStruct obj{.x = 42};

  fit::result result = ::fidl::Persist(obj);
  ASSERT_TRUE(result.is_ok()) << "Error during persist: " << result.error_value();

  // clang-format off
  const std::vector<uint8_t> golden_bytes = {
      // Wire format metadata.
      0, kFidlWireFormatMagicNumberInitial, FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0,
      0, 0, 0, 0,
      // Payload, a single uint32_t.
      42, 0, 0, 0, 0, 0, 0, 0,
  };
  // clang-format on

  EXPECT_EQ(result.value(), golden_bytes);
}

TEST(Unpersist, WireUnion) {
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Wire format metadata.
      0, kFidlWireFormatMagicNumberInitial, FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0,
      0, 0, 0, 0,
      // Payload, a union with ordinal 1, and inlined int32.
      1, 0, 0, 0, 0, 0, 0, 0,
      42, 0, 0, 0, 0, 0, 1, 0,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 24U);

  fit::result result =
      ::fidl::InplaceUnpersist<test_types::wire::TestStrictXUnion>(cpp20::span(bytes));
  ASSERT_TRUE(result.is_ok()) << "Error during unpersist: " << result.error_value();
  test_types::wire::TestStrictXUnion& obj = *result.value();

  // Check decoded value.
  EXPECT_TRUE(obj.is_primitive());
  EXPECT_EQ(42, obj.primitive());
}

TEST(Persist, WireUnion) {
  auto obj = test_types::wire::TestStrictXUnion::WithPrimitive(42);

  fit::result result = ::fidl::Persist(obj);
  ASSERT_TRUE(result.is_ok()) << "Error during persist: " << result.error_value();

  // clang-format off
  const std::vector<uint8_t> golden_bytes = {
      // Wire format metadata.
      0, kFidlWireFormatMagicNumberInitial, FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0,
      0, 0, 0, 0,
      // Payload, a union with ordinal 1, and inlined int32.
      1, 0, 0, 0, 0, 0, 0, 0,
      42, 0, 0, 0, 0, 0, 1, 0,
  };
  // clang-format on

  EXPECT_EQ(result.value(), golden_bytes);
}

TEST(Unpersist, WireTable) {
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Wire format metadata.
      0, kFidlWireFormatMagicNumberInitial, FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0,
      0, 0, 0, 0,
      // Payload, an empty table.
      0, 0, 0, 0, 0, 0, 0, 0,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 24U);

  fit::result result =
      ::fidl::InplaceUnpersist<test_types::wire::SampleEmptyTable>(cpp20::span(bytes));
  ASSERT_TRUE(result.is_ok()) << "Error during unpersist: " << result.error_value();
  test_types::wire::SampleEmptyTable& obj = *result.value();

  // Check decoded value.
  EXPECT_TRUE(obj.IsEmpty());
}

TEST(Persist, WireTable) {
  test_types::wire::SampleEmptyTable obj;

  fit::result result = ::fidl::Persist(obj);
  ASSERT_TRUE(result.is_ok()) << "Error during persist: " << result.error_value();

  // clang-format off
  const std::vector<uint8_t> golden_bytes = {
      // Wire format metadata.
      0, kFidlWireFormatMagicNumberInitial, FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0,
      0, 0, 0, 0,
      // Payload, an empty table.
      0, 0, 0, 0, 0, 0, 0, 0,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };
  // clang-format on

  EXPECT_EQ(result.value(), golden_bytes);
}
