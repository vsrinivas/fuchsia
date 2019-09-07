// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <zxtest/zxtest.h>

TEST(VectorView, AdaptorTest) {
  std::vector<uint32_t> vector({1, 2, 3});
  fidl::VectorView view(vector);
  static_assert(std::is_same_v<decltype(view.data()), const uint32_t*>);
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.data(), vector.data());
  EXPECT_EQ(view.count(), vector.size());

  // Compile-time tests for fidl::VectorView constructor
  std::vector<const uint32_t> const_vec;
  fidl::VectorView const_view(const_vec);
  static_assert(std::is_same_v<decltype(const_view.mutable_data()), const uint32_t*>);
}

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
