// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

#include <zxtest/zxtest.h>

TEST(StringView, AdaptorTest) {
  std::string str = "abc";
  fidl::StringView view(str);
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.data(), str.data());
  EXPECT_EQ(view.size(), str.size());
}

TEST(StringView, StaticConstructionTest) {
  fidl::StringView view("abc");
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.size(), 3);
  EXPECT_STR_EQ(view.data(), "abc");

  fidl::StringView empty("");
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.size(), 0);
  EXPECT_NOT_NULL(empty.data());
}

TEST(StringView, DynamicConstructionTest) {
  char* hello = strdup("hello");
  fidl::StringView view(hello, strlen(hello));
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.size(), 5);
  EXPECT_STR_EQ(view.data(), "hello");
  free(static_cast<void*>(hello));
}
