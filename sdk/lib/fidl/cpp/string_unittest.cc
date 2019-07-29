// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/string.h"

#include "gtest/gtest.h"

namespace fidl {
namespace {

TEST(StringPtr, Control) {
  StringPtr string;
  EXPECT_TRUE(string.is_null());
  EXPECT_FALSE(string);
  string->append("abc");
  EXPECT_FALSE(string.is_null());
  EXPECT_TRUE(string);

  string.reset("hello, world");
  EXPECT_FALSE(string.is_null());
  EXPECT_TRUE(string);
  EXPECT_EQ("hello, world", *string);
  EXPECT_EQ("hello, world", string.get());
  EXPECT_EQ(12u, string->size());

  StringPtr other(std::move(string));
  EXPECT_EQ("hello, world", *other);

  StringPtr other2 = other;
  EXPECT_EQ("hello, world", *other);
  EXPECT_EQ("hello, world", *other2);
  EXPECT_EQ(other, other2);

  other2.reset();
  EXPECT_TRUE(other2.is_null());

  other2.swap(other);
  EXPECT_TRUE(other.is_null());
  EXPECT_EQ("hello, world", *other2);
  EXPECT_NE(other, other2);
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
  EXPECT_EQ("", *null);

  std::string helloStr = hello;
  EXPECT_EQ("hello", helloStr);

  std::string nullStr = null;
  EXPECT_EQ("", nullStr);
}

TEST(StringPtr, Map) {
  std::map<StringPtr, int> map;
  StringPtr a = "a";
  StringPtr b = "b";
  StringPtr null;

  map[a] = 1;
  map[b] = 2;
  map[null] = 3;

  EXPECT_EQ(1, map[a]);
  EXPECT_EQ(2, map[b]);
  EXPECT_EQ(3, map[null]);
}

TEST(StringPtr, FitOptional) {
  StringPtr hello = "hello";
  EXPECT_TRUE(hello.has_value());
  EXPECT_TRUE(hello);
  EXPECT_EQ(*hello, "hello");
  EXPECT_EQ(hello->length(), 5u);
  EXPECT_EQ(hello.value(), "hello");
  EXPECT_EQ(hello.value_or("bye"), "hello");
  EXPECT_TRUE(hello.has_value());
  EXPECT_EQ(hello.value(), "hello");

  hello.reset();
  EXPECT_FALSE(hello.has_value());
  EXPECT_FALSE(hello);
  EXPECT_EQ(hello.value_or("bye"), "bye");

  StringPtr greeting = "hi";
  hello.swap(greeting);
  EXPECT_TRUE(hello.has_value());
  EXPECT_FALSE(greeting.has_value());
  EXPECT_EQ(hello.value(), "hi");
}

}  // namespace
}  // namespace fidl
