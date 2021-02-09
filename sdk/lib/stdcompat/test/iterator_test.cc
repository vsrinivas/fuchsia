// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/iterator.h>

#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(DataTest, ObtainsPointerFromContainerType) {
  std::vector<int> a;
  const auto& b = a;

  EXPECT_EQ(cpp17::data(a), a.data());
  EXPECT_EQ(cpp17::data(b), b.data());
}

TEST(DataTest, ObtainsPointerFromArrayType) {
  int a[5];
  const auto& b = a;

  EXPECT_EQ(cpp17::data(a), a);
  EXPECT_EQ(cpp17::data(b), b);
}

TEST(DataTest, ObtainsPointersinitializerList) {
  std::initializer_list<int> a = {1, 2, 3, 4};
  const auto& b = a;

  EXPECT_EQ(cpp17::data(a), a.begin());
  EXPECT_EQ(cpp17::data(b), b.begin());
}

TEST(SizeTest, ObtainsPointerFromContainerType) {
  std::vector<int> a;
  const auto& b = a;

  EXPECT_EQ(cpp17::size(a), a.size());
  EXPECT_EQ(cpp17::size(b), b.size());
}

TEST(SizeTest, ObtainsPointerFromArrayType) {
  int a[5];
  const auto& b = a;

  EXPECT_EQ(cpp17::size(a), 5u);
  EXPECT_EQ(cpp17::size(b), 5u);
}

TEST(SizeTest, ObtainsPointersinitializerList) {
  std::initializer_list<int> a = {1, 2, 3, 4};
  const auto& b = a;

  EXPECT_EQ(cpp17::size(a), a.size());
  EXPECT_EQ(cpp17::size(b), b.size());
}

#if __cpp_lib_nonmember_container_access >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(DataTest, AliasWhenStdIsAvailable) {
  {
    // Need so the compiler picks the right overload.
    constexpr int* (*cpp17_data)(int(&)[5]) = &cpp17::data<int>;
    constexpr int* (*std_data)(int(&)[5]) = &std::data<int>;
    static_assert(cpp17_data == std_data,
                  "Specialization for cpp17::data is not an alias for std::data.");
  }

  {
    constexpr int* (*cpp17_data)(std::vector<int>&) = &cpp17::data<std::vector<int>>;
    constexpr int* (*std_data)(std::vector<int>&) = &std::data<std::vector<int>>;
    static_assert(cpp17_data == std_data,
                  "Specialization for cpp17::data is not an alias for std::data.");
  }

  {
    constexpr const int* (*cpp17_data)(const std::vector<int>&) = &cpp17::data<std::vector<int>>;
    constexpr const int* (*std_data)(const std::vector<int>&) = &std::data<std::vector<int>>;
    static_assert(cpp17_data == std_data,
                  "Specialization for cpp17::data is not an alias for std::data.");
  }

  {
    constexpr const int* (*cpp17_data)(std::initializer_list<int>) = &cpp17::data<int>;
    constexpr const int* (*std_data)(std::initializer_list<int>) = &std::data<int>;
    static_assert(cpp17_data == std_data,
                  "Specialization for cpp17::data is not an alias for std::data.");
  }
}

TEST(SizeTest, AliasWhenStdIsAvailable) {
  {
    // Need so the compiler picks the right overload.
    constexpr size_t (*cpp17_size)(const int(&)[5]) = &cpp17::size<int>;
    constexpr size_t (*std_size)(const int(&)[5]) = &std::size<int>;
    static_assert(cpp17_size == std_size,
                  "Specialization for cpp17::size is not an alias for std::size.");
  }

  {
    constexpr size_t (*cpp17_size)(const std::vector<int>&) = &cpp17::size<std::vector<int>>;
    constexpr size_t (*std_size)(const std::vector<int>&) = &std::size<std::vector<int>>;
    static_assert(cpp17_size == std_size,
                  "Specialization for cpp17::size is not an alias for std::size.");
  }

  {
    constexpr size_t (*cpp17_size)(const std::initializer_list<int>&) =
        &cpp17::size<std::initializer_list<int>>;
    constexpr size_t (*std_size)(const std::initializer_list<int>&) =
        &std::size<std::initializer_list<int>>;
    static_assert(cpp17_size == std_size,
                  "Specialization for cpp17::size is not an alias for std::size.");
  };
}

#endif

}  // namespace
