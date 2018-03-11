// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/string.h"

namespace fidl {
namespace {

TEST(StringPtr, Control) {
  StringPtr string;
  EXPECT_TRUE(string.is_null());
  EXPECT_FALSE(string);

  string.reset("hello, world");
  EXPECT_FALSE(string.is_null());
  EXPECT_TRUE(string);
  EXPECT_EQ("hello, world", *string);
  EXPECT_EQ("hello, world", string.get());
  EXPECT_EQ(12u, string->size());

  StringPtr other(std::move(string));
  EXPECT_EQ("hello, world", *other);
}

TEST(StringPtr, Conversions) {
  StringPtr hello = "hello";
  EXPECT_FALSE(hello.is_null());
  EXPECT_EQ("hello", *hello);

  StringPtr world("world", 5);
  EXPECT_FALSE(world.is_null());
  EXPECT_EQ("world", *world);

  StringPtr null = nullptr;
  EXPECT_TRUE(null.is_null());
}

}  // namespace
}  // namespace fidl
