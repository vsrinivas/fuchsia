// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/string.h"

#include <lib/stdcompat/string_view.h>

#include <zxtest/zxtest.h>

namespace fidl {
namespace {

TEST(StringPtr, Control) {
  StringPtr string;
  EXPECT_FALSE(string.has_value());
  EXPECT_FALSE(string);

  string = "hello, world";
  EXPECT_TRUE(string.has_value());
  EXPECT_TRUE(string);
  EXPECT_EQ("hello, world", *string);
  EXPECT_EQ("hello, world", string.value());
  EXPECT_EQ(12u, string->size());

  StringPtr other(std::move(string));
  EXPECT_EQ("hello, world", *other);

  StringPtr other2 = other;
  EXPECT_EQ("hello, world", *other);
  EXPECT_EQ("hello, world", *other2);
  EXPECT_EQ(other, other2);

  other2.reset();
  EXPECT_FALSE(other2.has_value());

  other2.swap(other);
  EXPECT_FALSE(other.has_value());
  EXPECT_EQ("hello, world", *other2);
  EXPECT_NE(other, other2);
}

TEST(StringPtr, Conversions) {
  // const char*.
  {
    constexpr const char* kHello = "hello";
    constexpr const char* kWorld = "world";

    StringPtr hello = kHello;
    EXPECT_TRUE(hello.has_value());
    EXPECT_EQ(kHello, *hello);

    StringPtr world(kWorld);
    EXPECT_TRUE(world.has_value());
    EXPECT_EQ(kWorld, *world);
  }

  // cpp17::string_view.
  {
    constexpr cpp17::string_view kHello = "hello";
    constexpr cpp17::string_view kWorld = "world";

    StringPtr hello = kHello;
    EXPECT_TRUE(hello.has_value());
    EXPECT_EQ(kHello, *hello);

    StringPtr world(kWorld);
    EXPECT_TRUE(world.has_value());
    EXPECT_EQ(kWorld, *world);
  }

  // const std::string&.
  {
    const std::string kHello = "hello";
    const std::string kWorld = "world";

    StringPtr hello = kHello;
    EXPECT_TRUE(hello.has_value());
    EXPECT_EQ(kHello, *hello);

    StringPtr world(kWorld);
    EXPECT_TRUE(world.has_value());
    EXPECT_EQ(kWorld, *world);
  }

  // std::string&&.
  {
    std::string movable_hello = "hello";
    std::string movable_world = "world";

    StringPtr hello = std::move(movable_hello);
    EXPECT_TRUE(hello.has_value());
    EXPECT_EQ("hello", *hello);

    StringPtr world(std::move(movable_world));
    EXPECT_TRUE(world.has_value());
    EXPECT_EQ("world", *world);
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  StringPtr null = nullptr;
  EXPECT_FALSE(null.has_value());

  std::string helloStr = StringPtr("hello").value_or("");
  EXPECT_EQ("hello", helloStr);

  std::string nullStr = StringPtr(nullptr).value_or("");
  EXPECT_EQ("", nullStr);
#pragma GCC diagnostic pop
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
