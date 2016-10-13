// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/shared_buffer/strings.h"
#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(SharedBufferStrings, ShortString) {
  const std::string hello_string = "Hello, world.";
  mojo::ScopedSharedBufferHandle hello_buffer;
  EXPECT_TRUE(SharedBufferFromString(hello_string, &hello_buffer));
  std::string hello_out;
  EXPECT_TRUE(StringFromSharedBuffer(std::move(hello_buffer), &hello_out));
  EXPECT_EQ(hello_string, hello_out);
}

TEST(SharedBufferStrings, EmptyString) {
  const std::string hello_string = "";
  mojo::ScopedSharedBufferHandle hello_buffer;
  EXPECT_TRUE(SharedBufferFromString(hello_string, &hello_buffer));
  std::string hello_out;
  EXPECT_TRUE(StringFromSharedBuffer(std::move(hello_buffer), &hello_out));
  EXPECT_EQ(hello_string, hello_out);
}

TEST(SharedBufferStrings, BinaryString) {
  std::string binary_string('\0', 10);
  for (size_t i = 0; i < binary_string.size(); i++) {
    binary_string[i] = (char)i;
  }
  mojo::ScopedSharedBufferHandle binary_buffer;
  EXPECT_TRUE(SharedBufferFromString(binary_string, &binary_buffer));
  std::string binary_out;
  EXPECT_TRUE(StringFromSharedBuffer(std::move(binary_buffer), &binary_out));
  EXPECT_EQ(binary_string, binary_out);
}

}  // namespace
}  // namespace mtl
