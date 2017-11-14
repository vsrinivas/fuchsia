// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/vmo/strings.h"
#include "gtest/gtest.h"

namespace fsl {
namespace {

template <typename T>
class VmoStringsTest : public ::testing::Test {};

typedef ::testing::Types<zx::vmo, SizedVmo> VmoTypes;
TYPED_TEST_CASE(VmoStringsTest, VmoTypes);

TYPED_TEST(VmoStringsTest, ShortString) {
  const std::string hello_string = "Hello, world.";
  TypeParam hello_buffer;
  EXPECT_TRUE(VmoFromString(hello_string, &hello_buffer));
  std::string hello_out;
  EXPECT_TRUE(StringFromVmo(std::move(hello_buffer), &hello_out));
  EXPECT_EQ(hello_string, hello_out);
}

TYPED_TEST(VmoStringsTest, EmptyString) {
  const std::string hello_string = "";
  TypeParam hello_buffer;
  EXPECT_TRUE(VmoFromString(hello_string, &hello_buffer));
  std::string hello_out;
  EXPECT_TRUE(StringFromVmo(std::move(hello_buffer), &hello_out));
  EXPECT_EQ(hello_string, hello_out);
}

TYPED_TEST(VmoStringsTest, BinaryString) {
  std::string binary_string('\0', 10);
  for (size_t i = 0; i < binary_string.size(); i++) {
    binary_string[i] = (char)i;
  }
  TypeParam binary_buffer;
  EXPECT_TRUE(VmoFromString(binary_string, &binary_buffer));
  std::string binary_out;
  EXPECT_TRUE(StringFromVmo(std::move(binary_buffer), &binary_out));
  EXPECT_EQ(binary_string, binary_out);
}

}  // namespace
}  // namespace fsl
