// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/internal/utility.h>

#include <zxtest/zxtest.h>

namespace {

using fit::internal::first_t;
static_assert(std::is_same<int, first_t<int>>::value, "");
static_assert(std::is_same<int, first_t<int, char>>::value, "");
static_assert(std::is_same<int, first_t<int, char, bool>>::value, "");

using fit::internal::occurences_of_v;
static_assert(occurences_of_v<int> == 0, "");
static_assert(occurences_of_v<int, char> == 0, "");
static_assert(occurences_of_v<int, char, bool> == 0, "");
static_assert(occurences_of_v<int, int, bool> == 1, "");
static_assert(occurences_of_v<int, bool, int> == 1, "");
static_assert(occurences_of_v<int, int, int> == 2, "");
static_assert(occurences_of_v<int, char, bool, short> == 0, "");
static_assert(occurences_of_v<int, int, int, char> == 2, "");
static_assert(occurences_of_v<int, int, char, int> == 2, "");
static_assert(occurences_of_v<int, char, int, int> == 2, "");
static_assert(occurences_of_v<int, int, int, int> == 3, "");

using fit::internal::remove_cvref_t;
static_assert(std::is_same<int, remove_cvref_t<const volatile int&>>::value, "");

using fit::internal::not_same_type;
static_assert(!not_same_type<int, const volatile int&>::value, "");

struct trivially_destructible {
  ~trivially_destructible() = default;
};
struct non_trivially_destructible {
  ~non_trivially_destructible() {}
};

using fit::internal::is_trivially_destructible_v;
static_assert(is_trivially_destructible_v<> == true, "");
static_assert(is_trivially_destructible_v<trivially_destructible> == true, "");
static_assert(is_trivially_destructible_v<trivially_destructible, trivially_destructible> == true,
              "");
static_assert(is_trivially_destructible_v<non_trivially_destructible> == false, "");
static_assert(is_trivially_destructible_v<non_trivially_destructible, non_trivially_destructible> ==
                  false,
              "");
static_assert(is_trivially_destructible_v<trivially_destructible, non_trivially_destructible> ==
                  false,
              "");
static_assert(is_trivially_destructible_v<non_trivially_destructible, trivially_destructible> ==
                  false,
              "");

struct trivially_copyable {
  trivially_copyable() = default;
  trivially_copyable(const trivially_copyable&) = default;
  trivially_copyable& operator=(const trivially_copyable&) = default;
  ~trivially_copyable() = default;
};

struct non_trivially_copyable {
  non_trivially_copyable() {}
  non_trivially_copyable(const trivially_copyable&) {}
  non_trivially_copyable& operator=(const trivially_copyable&) { return *this; }
  ~non_trivially_copyable() {}
};

using fit::internal::is_trivially_copyable_v;
static_assert(is_trivially_copyable_v<> == true, "");
static_assert(is_trivially_copyable_v<trivially_copyable> == true, "");
static_assert(is_trivially_copyable_v<trivially_copyable, trivially_copyable> == true, "");
static_assert(is_trivially_copyable_v<non_trivially_copyable> == false, "");
static_assert(is_trivially_copyable_v<non_trivially_copyable, non_trivially_copyable> == false, "");
static_assert(is_trivially_copyable_v<trivially_copyable, non_trivially_copyable> == false, "");
static_assert(is_trivially_copyable_v<non_trivially_copyable, trivially_copyable> == false, "");

struct trivially_movable {
  trivially_movable() = default;
  trivially_movable(trivially_movable&&) = default;
  trivially_movable& operator=(trivially_movable&&) = default;
  ~trivially_movable() = default;
};

struct non_trivially_movable {
  non_trivially_movable() {}
  non_trivially_movable(trivially_movable&&) {}
  non_trivially_movable& operator=(trivially_movable&&) { return *this; }
  ~non_trivially_movable() {}
};

using fit::internal::is_trivially_movable_v;
static_assert(is_trivially_movable_v<> == true, "");
static_assert(is_trivially_movable_v<trivially_movable> == true, "");
static_assert(is_trivially_movable_v<trivially_movable, trivially_movable> == true, "");
static_assert(is_trivially_movable_v<non_trivially_movable> == false, "");
static_assert(is_trivially_movable_v<non_trivially_movable, non_trivially_movable> == false, "");
static_assert(is_trivially_movable_v<trivially_movable, non_trivially_movable> == false, "");
static_assert(is_trivially_movable_v<non_trivially_movable, trivially_movable> == false, "");

using fit::internal::enable_relop_t;
template <typename T, typename U, typename... Conditions,
          enable_relop_t<decltype(std::declval<T>() == std::declval<U>()), Conditions...> = true>
constexpr bool is_comparable(T&&, U&&, Conditions&&...) {
  return true;
}
constexpr bool is_comparable(...) { return false; }

struct comparable_a {};
struct comparable_b {};
struct comparable_c {};
struct not_bool {};

// A and B are comparable to themselves and each other.
[[maybe_unused]] constexpr bool operator==(const comparable_a&, const comparable_a&) {
  return true;
}
[[maybe_unused]] constexpr bool operator==(const comparable_b&, const comparable_b&) {
  return true;
}
[[maybe_unused]] constexpr bool operator==(const comparable_a&, const comparable_b&) {
  return true;
}
[[maybe_unused]] constexpr bool operator==(const comparable_b&, const comparable_a&) {
  return true;
}

// C is comparable with itself but the result is not convertible to bool.
[[maybe_unused]] constexpr not_bool operator==(const comparable_c&, const comparable_c&) {
  return {};
}

static_assert(is_comparable(comparable_a{}, comparable_a{}), "");
static_assert(is_comparable(comparable_b{}, comparable_b{}), "");
static_assert(is_comparable(comparable_a{}, comparable_b{}), "");
static_assert(is_comparable(comparable_b{}, comparable_a{}), "");

static_assert(!is_comparable(comparable_a{}, comparable_a{}, std::false_type{}), "");
static_assert(!is_comparable(comparable_b{}, comparable_b{}, std::false_type{}), "");
static_assert(!is_comparable(comparable_a{}, comparable_b{}, std::false_type{}), "");
static_assert(!is_comparable(comparable_b{}, comparable_a{}, std::false_type{}), "");

static_assert(!is_comparable(comparable_a{}, comparable_c{}), "");
static_assert(!is_comparable(comparable_c{}, comparable_a{}), "");
static_assert(!is_comparable(comparable_b{}, comparable_c{}), "");
static_assert(!is_comparable(comparable_c{}, comparable_b{}), "");
static_assert(!is_comparable(comparable_c{}, comparable_c{}), "");

}  // anonymous namespace
