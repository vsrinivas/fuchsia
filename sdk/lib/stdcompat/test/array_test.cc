// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/array.h>
#include <lib/stdcompat/type_traits.h>

#include <array>

#include "gtest.h"

namespace {

struct CopyOnly {
  CopyOnly() = default;
  CopyOnly(const CopyOnly&) = default;
  CopyOnly(CopyOnly&&) = delete;

  int a;
};

struct MoveOnly {
  MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;

  int a;
};

TEST(ToArrayTest, InitializesFromCopyOnlyType) {
  CopyOnly copyvals[10];
  auto typed_array = cpp20::to_array(copyvals);

  static_assert(cpp17::is_same_v<decltype(typed_array), std::array<CopyOnly, 10>>, "");
}

TEST(ToArrayTest, InitializesFromMoveOnlyType) {
  MoveOnly movevals[10];
  auto typed_array = cpp20::to_array(std::move(movevals));

  static_assert(cpp17::is_same_v<decltype(typed_array), std::array<MoveOnly, 10>>, "");
}

TEST(ToArrayTest, InitializesFromInitializerList) {
  auto typed_array = cpp20::to_array({1, 2, 3, 4});

  static_assert(cpp17::is_same_v<decltype(typed_array), std::array<int, 4>>, "");
}

#if defined(__cpp_lib_to_array) && __cpp_lib_to_array >= 201907L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(ToArrayTest, IsAliasWhenStdIsAvailable) {
  {
    constexpr std::array<CopyOnly, 10> (*to_array_cpp20)(CopyOnly(&)[10]) = cpp20::to_array;
    constexpr std::array<CopyOnly, 10> (*to_array_std)(CopyOnly(&)[10]) = std::to_array;

    static_assert(to_array_cpp20 == to_array_std,
                  "cpp20::to_array should be an alias of std::to_array.");
  }

  {
    constexpr std::array<MoveOnly, 10> (*to_array_cpp20)(MoveOnly(&&)[10]) = cpp20::to_array;
    constexpr std::array<MoveOnly, 10> (*to_array_std)(MoveOnly(&&)[10]) = std::to_array;

    static_assert(to_array_cpp20 == to_array_std,
                  "cpp20::to_array should be an alias of std::to_array.");
  }
}

#endif  // __cpp_lib_to_array >= 201907L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace
