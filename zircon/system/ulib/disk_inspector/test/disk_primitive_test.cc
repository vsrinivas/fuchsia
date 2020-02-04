// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_primitive.h"

#include <disk_inspector/supported_types.h>
#include <zxtest/zxtest.h>

namespace disk_inspector {
namespace {

TEST(DiskPrimitiveTest, StringToUintSuccess) {
  uint8_t value = 8;
  std::string string_value = "8";
  uint8_t result;
  EXPECT_OK(internal::StringToUint(string_value, &result));
  EXPECT_EQ(result, value);

  uint64_t large_value = 0x1ffffffff;  // Larger than uint32_t max.
  string_value = "8589934591";
  uint64_t large_result;
  EXPECT_OK(internal::StringToUint(string_value, &large_result));
  EXPECT_EQ(large_result, large_value);
}

TEST(DiskPrimitiveTest, StringToUintNotAnInt) {
  std::string input = "testing123";
  uint64_t result;
  EXPECT_EQ(internal::StringToUint(input, &result), ZX_ERR_INVALID_ARGS);
}

TEST(DiskPrimitiveTest, StringToUintValueTooLarge) {
  std::string string_value = "8589934591";  // 0x1ffffffff
  uint32_t result;
  ASSERT_EQ(internal::StringToUint(string_value, &result), ZX_ERR_INVALID_ARGS);
}

TEST(DiskPrimitiveTest, WriteField) {
  uint64_t value = 0;
  uint64_t target = 1234;
  Primitive<uint64_t> uint_object("uint64_t");
  ASSERT_OK(uint_object.WriteField(&value, {}, {}, std::to_string(target)));
  EXPECT_EQ(value, target);
}

TEST(DiskPrimitiveTest, GetHexString) {
  uint64_t value = 64;
  std::string result = "0x40";
  PrintOptions options;
  options.display_hex = true;
  Primitive<uint64_t> uint_object("uint64_t");
  EXPECT_STR_EQ(result, uint_object.ToString(&value, options));
}

TEST(DiskPrimitiveTest, GetString) {
  uint64_t value = 64;
  std::string result = "64";
  PrintOptions options;
  options.display_hex = false;
  Primitive<uint64_t> uint_object("uint64_t");
  EXPECT_STR_EQ(result, uint_object.ToString(&value, options));
}

}  // namespace
}  // namespace disk_inspector
