// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/memory.h>
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

  static_assert(cpp17::addressof(cpp17::in_place) == std::addressof(std::in_place));
  static_assert(cpp17::addressof(cpp17::in_place_type<void>) ==
                std::addressof(std::in_place_type<void>));
  static_assert(cpp17::addressof(cpp17::in_place_index<0>) ==
                std::addressof(std::in_place_index<0>));
#else  // Force template instantiation.

  // Sanity checks that the instantiations are actually different for the polyfills.
  static_assert(cpp17::addressof(cpp17::in_place) != nullptr);
  static_assert(static_cast<const void*>(cpp17::addressof(cpp17::in_place_type<void>)) !=
                static_cast<const void*>(cpp17::addressof(std::in_place_type<int>)));
  static_assert(static_cast<const void*>(cpp17::addressof(cpp17::in_place_index<0>)) !=
                static_cast<const void*>(cpp17::addressof(std::in_place_index<1>)));
#endif
}

constexpr bool ExchangeCheck() {
  int a = 1;
  int b = 2;

  b = cpp20::exchange(a, std::move(b));

  return a == 2 && b == 1;
}

constexpr bool ExchangeCheck2() {
  cpp17::string_view a = "1";
  cpp17::string_view b = "2";

  b = cpp20::exchange(a, std::move(b));

  return a == "2" && b == "1";
}

bool ExchangeCheck3() {
  cpp17::string_view a = "1";
  cpp17::string_view b = "2";

  b = cpp20::exchange(a, std::move(b));

  return a == "2" && b == "1";
}

TEST(ExchangeTest, IsConstexpr) {
  static_assert(ExchangeCheck(), "exchange evaluates incorrectly in constexpr context.");
  static_assert(ExchangeCheck2(), "exchange evaluates incorrectly in constexpr context.");
}

TEST(ExchangeTest, Runtime) { ASSERT_TRUE(ExchangeCheck3()); }

#if __cpp_lib_constexpr_algorithms >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(ExchangeTest, IsAliasWhenAvailable) {
  constexpr int (*cpp20_exchange)(int&, int&&) = &cpp20::exchange<int>;
  constexpr int (*std_exchange)(int&, int&&) = &std::exchange<int>;
  static_assert(cpp20_exchange == std_exchange,
                "cpp20::exchange must be an alias for std::exchange in c++20.");
}

#endif

template <typename T, typename = void>
struct matches_as_const : std::false_type {};

template <typename T>
struct matches_as_const<T, cpp17::void_t<decltype(cpp17::as_const(std::declval<T>()))>>
    : std::true_type {};

template <typename T>
static constexpr bool matches_as_const_v = matches_as_const<T>::value;

template <typename T>
constexpr bool match(T&& value) {
  return matches_as_const_v<T>;
}

TEST(AsConstTest, ReturnsConstReference) {
  int i = 0;
  const int ci = 0;

  static_assert(cpp17::is_same_v<decltype(cpp17::as_const(i)), const int&>, "");
  static_assert(cpp17::is_same_v<decltype(cpp17::as_const(ci)), const int&>, "");
}

TEST(AsConstTest, DeniesRvalueReferences) {
  int i = 0;
  const int ci = 0;

  static_assert(match(i) == true, "cpp17::as_const should accept lvalue references.");
  static_assert(match(std::move(i)) == false,
                "cpp17::as_const should not accept rvalue references.");
  static_assert(match(ci) == true, "cpp17::as_const should accept lvalue references.");
  static_assert(match(std::move(ci)) == false,
                "cpp17::as_const should not accept rvalue references.");
}

#if __cpp_lib_as_const >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(AsConstTest, IsAliasWhenAvailable) {
  constexpr const int& (*cpp17_as_const)(int&) = &cpp17::as_const<int>;
  constexpr const int& (*std_as_const)(int&) = &std::as_const<int>;
  static_assert(cpp17_as_const == std_as_const,
                "cpp17::as_const must be an alias for std::as_const in c++17.");
}

#endif

}  // namespace
