// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/string_view.h>
#include <lib/stdcompat/utility.h>

#include <gtest/gtest.h>

namespace {

TEST(InplaceTagTest, InplaceTagsSwitchToStdProvidedOnStd17) {
  static_assert(std::is_trivially_default_constructible<cpp17::in_place_t>::value);
  static_assert(std::is_trivially_default_constructible<cpp17::in_place_index_t<0>>::value);
  static_assert(std::is_trivially_default_constructible<cpp17::in_place_type_t<void>>::value);

#if __cplusplus >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)
  static_assert(std::is_same<cpp17::in_place_t, std::in_place_t>::value);
  static_assert(std::is_same<cpp17::in_place_type_t<void>, std::in_place_type_t<void>>::value);
  static_assert(std::is_same<cpp17::in_place_index_t<0>, std::in_place_index_t<0>>::value);

  static_assert(std::addressof(cpp17::in_place) == std::addressof(std::in_place));
  static_assert(std::addressof(cpp17::in_place_type<void>) ==
                std::addressof(std::in_place_type<void>));
  static_assert(std::addressof(cpp17::in_place_index<0>) == std::addressof(std::in_place_index<0>));
#else  // Force template instantiation.

  // Sanity checks that the instantiations are actually different for the polyfills.
  static_assert(std::addressof(cpp17::in_place) != nullptr);
  static_assert(static_cast<const void*>(std::addressof(cpp17::in_place_type<void>)) !=
                static_cast<const void*>(std::addressof(std::in_place_type<int>)));
  static_assert(static_cast<const void*>(std::addressof(cpp17::in_place_index<0>)) !=
                static_cast<const void*>(std::addressof(std::in_place_index<1>)));
#endif
}

constexpr bool SwapCheck() {
  int a = 1;
  int b = 2;

  cpp20::swap(a, b);

  return a == 2 && b == 1;
}

constexpr bool SwapCheck2() {
  cpp17::string_view a = "1";
  cpp17::string_view b = "2";

  cpp20::swap(a, b);

  return a == "2" && b == "1";
}

TEST(SwapTest, IsConstexpr) {
  static_assert(SwapCheck(), "swap evaluates incorrectly in constexpr context.");
  static_assert(SwapCheck2(), "swap evaluates incorrectly in constexpr context.");
}

#if __cpp_lib_constexpr_algorithms >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(SwapTest, IsAlisWhenAvailable) {
  static_assert(&cpp17::swap<int> == &std::swap<int>,
                "cpp20::swap must be an alias for std::swap in c++20.");
}

#endif

}  // namespace
