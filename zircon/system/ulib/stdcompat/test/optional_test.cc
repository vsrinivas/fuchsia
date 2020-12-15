// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/optional.h>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// So that we get our implementation of fit::optional, not std::optional even on C++17
#define FORCE_FIT_OPTIONAL
#include <lib/fit/optional.h>

#include <zxtest/zxtest.h>

#include "unittest_utils.h"

namespace {

template <bool define_assignment_operators>
struct base {};
template <>
struct base<false> {
  base& operator=(const base& other) = delete;
  base& operator=(base&& other) = delete;
};

template <bool define_assignment_operators>
struct slot : base<define_assignment_operators> {
  slot(int value = 0) : value(value) { balance++; }
  slot(const slot& other) : value(other.value) { balance++; }
  slot(slot&& other) : value(other.value) { balance++; }

  ~slot() {
    ASSERT_CRITICAL(balance > 0);
    ASSERT_CRITICAL(value != -1);
    value = -1;  // sentinel to catch double-delete
    balance--;
  }

  static int balance;  // net constructor/destructor pairings
  int value;

  int get() const { return value; }
  int increment() { return ++value; }

  slot& operator=(const slot& other) = default;
  slot& operator=(slot&& other) = default;

  bool operator==(const slot& other) const { return value == other.value; }
  bool operator!=(const slot& other) const { return value != other.value; }

  void swap(slot& other) {
    const int temp = value;
    value = other.value;
    other.value = temp;
  }
};
template <>
int slot<false>::balance = 0;
template <>
int slot<true>::balance = 0;

template <bool assignment>
void swap(slot<assignment>& a, slot<assignment>& b) {
  a.swap(b);
}

// Test optional::value_type.
static_assert(std::is_same<int, fit::optional<int>::value_type>::value, "");

// Test basic constexpr context.
static_assert(fit::optional<int>{}.has_value() == false, "");
static_assert(fit::optional<int>{10}.has_value() == true, "");
static_assert(fit::optional<int>{10U}.has_value() == true, "");

// Test comparisons.
namespace comparison_tests {
struct greater {};
struct less {};

constexpr bool operator==(greater, greater) { return true; }
constexpr bool operator<=(greater, greater) { return true; }
constexpr bool operator>=(greater, greater) { return true; }
constexpr bool operator!=(greater, greater) { return false; }
constexpr bool operator<(greater, greater) { return false; }
constexpr bool operator>(greater, greater) { return false; }

constexpr bool operator==(less, less) { return true; }
constexpr bool operator<=(less, less) { return true; }
constexpr bool operator>=(less, less) { return true; }
constexpr bool operator!=(less, less) { return false; }
constexpr bool operator<(less, less) { return false; }
constexpr bool operator>(less, less) { return false; }

constexpr bool operator==(greater, less) { return false; }
constexpr bool operator<=(greater, less) { return false; }
constexpr bool operator>=(greater, less) { return true; }
constexpr bool operator!=(greater, less) { return true; }
constexpr bool operator<(greater, less) { return false; }
constexpr bool operator>(greater, less) { return true; }

constexpr bool operator==(less, greater) { return false; }
constexpr bool operator<=(less, greater) { return true; }
constexpr bool operator>=(less, greater) { return false; }
constexpr bool operator!=(less, greater) { return true; }
constexpr bool operator<(less, greater) { return true; }
constexpr bool operator>(less, greater) { return false; }

// Note these definitions match the empty-to-other, other-to-empty, and
// empty-to-empty comparison behavior of fit::optional for convenience in
// exhaustive testing.
constexpr bool operator==(fit::nullopt_t, greater) { return false; }
constexpr bool operator<=(fit::nullopt_t, greater) { return true; }
constexpr bool operator>=(fit::nullopt_t, greater) { return false; }
constexpr bool operator!=(fit::nullopt_t, greater) { return true; }
constexpr bool operator<(fit::nullopt_t, greater) { return true; }
constexpr bool operator>(fit::nullopt_t, greater) { return false; }

constexpr bool operator==(greater, fit::nullopt_t) { return false; }
constexpr bool operator<=(greater, fit::nullopt_t) { return false; }
constexpr bool operator>=(greater, fit::nullopt_t) { return true; }
constexpr bool operator!=(greater, fit::nullopt_t) { return true; }
constexpr bool operator<(greater, fit::nullopt_t) { return false; }
constexpr bool operator>(greater, fit::nullopt_t) { return true; }

constexpr bool operator==(fit::nullopt_t, less) { return false; }
constexpr bool operator<=(fit::nullopt_t, less) { return true; }
constexpr bool operator>=(fit::nullopt_t, less) { return false; }
constexpr bool operator!=(fit::nullopt_t, less) { return true; }
constexpr bool operator<(fit::nullopt_t, less) { return true; }
constexpr bool operator>(fit::nullopt_t, less) { return false; }

constexpr bool operator==(less, fit::nullopt_t) { return false; }
constexpr bool operator<=(less, fit::nullopt_t) { return false; }
constexpr bool operator>=(less, fit::nullopt_t) { return true; }
constexpr bool operator!=(less, fit::nullopt_t) { return true; }
constexpr bool operator<(less, fit::nullopt_t) { return false; }
constexpr bool operator>(less, fit::nullopt_t) { return true; }

constexpr bool operator==(fit::nullopt_t, fit::nullopt_t) { return true; }
constexpr bool operator<=(fit::nullopt_t, fit::nullopt_t) { return true; }
constexpr bool operator>=(fit::nullopt_t, fit::nullopt_t) { return true; }
constexpr bool operator!=(fit::nullopt_t, fit::nullopt_t) { return false; }
constexpr bool operator<(fit::nullopt_t, fit::nullopt_t) { return false; }
constexpr bool operator>(fit::nullopt_t, fit::nullopt_t) { return false; }

template <typename T, typename U>
constexpr bool match_comparisons(T lhs, U rhs) {
  // Both optional operands.
  static_assert((fit::optional<T>{lhs} == fit::optional<U>{rhs}) == (lhs == rhs), "");
  static_assert((fit::optional<T>{lhs} != fit::optional<U>{rhs}) == (lhs != rhs), "");
  static_assert((fit::optional<T>{lhs} <= fit::optional<U>{rhs}) == (lhs <= rhs), "");
  static_assert((fit::optional<T>{lhs} >= fit::optional<U>{rhs}) == (lhs >= rhs), "");
  static_assert((fit::optional<T>{lhs} < fit::optional<U>{rhs}) == (lhs < rhs), "");
  static_assert((fit::optional<T>{lhs} > fit::optional<U>{rhs}) == (lhs > rhs), "");

  static_assert((fit::optional<T>{} == fit::optional<U>{rhs}) == (fit::nullopt == rhs), "");
  static_assert((fit::optional<T>{} != fit::optional<U>{rhs}) == (fit::nullopt != rhs), "");
  static_assert((fit::optional<T>{} <= fit::optional<U>{rhs}) == (fit::nullopt <= rhs), "");
  static_assert((fit::optional<T>{} >= fit::optional<U>{rhs}) == (fit::nullopt >= rhs), "");
  static_assert((fit::optional<T>{} < fit::optional<U>{rhs}) == (fit::nullopt < rhs), "");
  static_assert((fit::optional<T>{} > fit::optional<U>{rhs}) == (fit::nullopt > rhs), "");

  static_assert((fit::optional<T>{lhs} == fit::optional<U>{}) == (lhs == fit::nullopt), "");
  static_assert((fit::optional<T>{lhs} != fit::optional<U>{}) == (lhs != fit::nullopt), "");
  static_assert((fit::optional<T>{lhs} <= fit::optional<U>{}) == (lhs <= fit::nullopt), "");
  static_assert((fit::optional<T>{lhs} >= fit::optional<U>{}) == (lhs >= fit::nullopt), "");
  static_assert((fit::optional<T>{lhs} < fit::optional<U>{}) == (lhs < fit::nullopt), "");
  static_assert((fit::optional<T>{lhs} > fit::optional<U>{}) == (lhs > fit::nullopt), "");

  static_assert((fit::optional<T>{} == fit::optional<U>{}) == (fit::nullopt == fit::nullopt), "");
  static_assert((fit::optional<T>{} != fit::optional<U>{}) == (fit::nullopt != fit::nullopt), "");
  static_assert((fit::optional<T>{} <= fit::optional<U>{}) == (fit::nullopt <= fit::nullopt), "");
  static_assert((fit::optional<T>{} >= fit::optional<U>{}) == (fit::nullopt >= fit::nullopt), "");
  static_assert((fit::optional<T>{} < fit::optional<U>{}) == (fit::nullopt < fit::nullopt), "");
  static_assert((fit::optional<T>{} > fit::optional<U>{}) == (fit::nullopt > fit::nullopt), "");

  // Right hand optional only.
  static_assert((lhs == fit::optional<U>{rhs}) == (lhs == rhs), "");
  static_assert((lhs != fit::optional<U>{rhs}) == (lhs != rhs), "");
  static_assert((lhs <= fit::optional<U>{rhs}) == (lhs <= rhs), "");
  static_assert((lhs >= fit::optional<U>{rhs}) == (lhs >= rhs), "");
  static_assert((lhs < fit::optional<U>{rhs}) == (lhs < rhs), "");
  static_assert((lhs > fit::optional<U>{rhs}) == (lhs > rhs), "");

  static_assert((lhs == fit::optional<U>{}) == (lhs == fit::nullopt), "");
  static_assert((lhs != fit::optional<U>{}) == (lhs != fit::nullopt), "");
  static_assert((lhs <= fit::optional<U>{}) == (lhs <= fit::nullopt), "");
  static_assert((lhs >= fit::optional<U>{}) == (lhs >= fit::nullopt), "");
  static_assert((lhs < fit::optional<U>{}) == (lhs < fit::nullopt), "");
  static_assert((lhs > fit::optional<U>{}) == (lhs > fit::nullopt), "");

  // Left hand optional only.
  static_assert((fit::optional<T>{lhs} == rhs) == (lhs == rhs), "");
  static_assert((fit::optional<T>{lhs} != rhs) == (lhs != rhs), "");
  static_assert((fit::optional<T>{lhs} <= rhs) == (lhs <= rhs), "");
  static_assert((fit::optional<T>{lhs} >= rhs) == (lhs >= rhs), "");
  static_assert((fit::optional<T>{lhs} < rhs) == (lhs < rhs), "");
  static_assert((fit::optional<T>{lhs} > rhs) == (lhs > rhs), "");

  static_assert((fit::optional<T>{} == rhs) == (fit::nullopt == rhs), "");
  static_assert((fit::optional<T>{} != rhs) == (fit::nullopt != rhs), "");
  static_assert((fit::optional<T>{} <= rhs) == (fit::nullopt <= rhs), "");
  static_assert((fit::optional<T>{} >= rhs) == (fit::nullopt >= rhs), "");
  static_assert((fit::optional<T>{} < rhs) == (fit::nullopt < rhs), "");
  static_assert((fit::optional<T>{} > rhs) == (fit::nullopt > rhs), "");

  return true;
}

static_assert(match_comparisons(greater{}, greater{}), "");
static_assert(match_comparisons(greater{}, less{}), "");
static_assert(match_comparisons(less{}, greater{}), "");
static_assert(match_comparisons(less{}, less{}), "");
}  // namespace comparison_tests

// Test trivial copy/move.
namespace trivial_copy_move_tests {
struct trivially_move_only {
  constexpr trivially_move_only(const trivially_move_only&) = delete;
  constexpr trivially_move_only& operator=(const trivially_move_only&) = delete;

  constexpr trivially_move_only(trivially_move_only&&) = default;
  constexpr trivially_move_only& operator=(trivially_move_only&&) = default;

  int value;
};

static_assert(std::is_trivially_copy_constructible<trivially_move_only>::value == false);
static_assert(std::is_trivially_copy_assignable<trivially_move_only>::value == false);
static_assert(std::is_trivially_move_constructible<trivially_move_only>::value == true);
static_assert(std::is_trivially_move_assignable<trivially_move_only>::value == true);

static_assert(std::is_trivially_copy_constructible<fit::optional<trivially_move_only>>::value ==
              false);
static_assert(std::is_trivially_copy_assignable<fit::optional<trivially_move_only>>::value ==
              false);
static_assert(std::is_trivially_move_constructible<fit::optional<trivially_move_only>>::value ==
              true);
static_assert(std::is_trivially_move_assignable<fit::optional<trivially_move_only>>::value == true);

struct trivially_copyable {
  constexpr trivially_copyable(const trivially_copyable&) = default;
  constexpr trivially_copyable& operator=(const trivially_copyable&) = default;

  int value;
};

static_assert(std::is_trivially_copy_constructible<trivially_copyable>::value == true);
static_assert(std::is_trivially_copy_assignable<trivially_copyable>::value == true);
static_assert(std::is_trivially_move_constructible<trivially_copyable>::value == true);
static_assert(std::is_trivially_move_assignable<trivially_copyable>::value == true);

static_assert(std::is_trivially_copy_constructible<fit::optional<trivially_copyable>>::value ==
              true);
static_assert(std::is_trivially_copy_assignable<fit::optional<trivially_copyable>>::value == true);
static_assert(std::is_trivially_move_constructible<fit::optional<trivially_copyable>>::value ==
              true);
static_assert(std::is_trivially_move_assignable<fit::optional<trivially_copyable>>::value == true);

}  // namespace trivial_copy_move_tests

template <typename T>
void construct_without_value() {
  fit::optional<T> opt;
  EXPECT_FALSE(opt.has_value());
  EXPECT_FALSE(!!opt);

  EXPECT_EQ(42, opt.value_or(T{42}).value);

  opt.reset();
  EXPECT_FALSE(opt.has_value());
}

template <typename T>
void construct_with_value() {
  fit::optional<T> opt(T{42});
  EXPECT_TRUE(opt.has_value());
  EXPECT_TRUE(!!opt);

  EXPECT_EQ(42, opt.value().value);
  EXPECT_EQ(42, opt.value_or(T{55}).value);

  EXPECT_EQ(42, opt->get());
  EXPECT_EQ(43, opt->increment());
  EXPECT_EQ(43, opt->get());

  opt.reset();
  EXPECT_FALSE(opt.has_value());
}

template <typename T>
void construct_copy() {
  fit::optional<T> a(T{42});
  fit::optional<T> b(a);
  fit::optional<T> c;
  fit::optional<T> d(c);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(d.has_value());
}

template <typename T>
void construct_move() {
  fit::optional<T> a(T{42});
  fit::optional<T> b(std::move(a));
  fit::optional<T> c;
  fit::optional<T> d(std::move(c));
  EXPECT_TRUE(a.has_value());
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(d.has_value());
}

template <typename T>
T get_value(fit::optional<T> opt) {
  return opt.value();
}

TEST(OptionalTests, construct_with_implicit_conversion) {
  // get_value expects a value of type fit::optional<T> but we pass 3
  // so this exercises the converting constructor
  EXPECT_EQ(3, get_value<int>(3));
}

template <typename T>
void accessors() {
  fit::optional<T> a(T{42});
  T& value = a.value();
  EXPECT_EQ(42, value.value);

  const T& const_value = const_cast<const decltype(a)&>(a).value();
  EXPECT_EQ(42, const_value.value);

  T rvalue = fit::optional<T>(T{42}).value();
  EXPECT_EQ(42, rvalue.value);

  T const_rvalue = const_cast<const fit::optional<T>&&>(fit::optional<T>(T{42})).value();
  EXPECT_EQ(42, const_rvalue.value);
}

template <typename T>
void assign() {
  fit::optional<T> a(T{42});
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);

  a = T{99};
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(99, a.value().value);

  a.reset();
  EXPECT_FALSE(a.has_value());

  a = T{55};
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);

  a = fit::nullopt;
  EXPECT_FALSE(a.has_value());
}

template <typename T>
void assign_copy() {
  fit::optional<T> a(T{42});
  fit::optional<T> b(T{55});
  fit::optional<T> c;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_FALSE(c.has_value());

  a = b;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);

  b = c;
  EXPECT_FALSE(b.has_value());
  EXPECT_FALSE(c.has_value());

  b = a;
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, b.value().value);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

  b = b;
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);

  c = c;
  EXPECT_FALSE(c.has_value());

#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

template <typename T>
void assign_move() {
  fit::optional<T> a(T{42});
  fit::optional<T> b(T{55});
  fit::optional<T> c;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_FALSE(c.has_value());

  a = std::move(b);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);
  EXPECT_TRUE(b.has_value());

  b = std::move(c);
  EXPECT_FALSE(b.has_value());
  EXPECT_FALSE(c.has_value());

  c = std::move(b);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(b.has_value());

  b = std::move(a);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_TRUE(a.has_value());

  b = std::move(b);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);

  a = std::move(a);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);

  c = std::move(c);
  EXPECT_FALSE(c.has_value());
}

template <typename T>
void emplace() {
  fit::optional<T> a;
  EXPECT_EQ(55, a.emplace(55).value);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);

  fit::optional<T> b(T{42});
  EXPECT_EQ(66, b.emplace(66).value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(66, b.value().value);
}

template <typename T>
void invoke() {
  fit::optional<T> a(T{42});
  EXPECT_EQ(42, a->get());
  EXPECT_EQ(43, a->increment());
  EXPECT_EQ(43, (*a).value);
}

template <typename T>
void comparisons() {
  fit::optional<T> a(T{42});
  fit::optional<T> b(T{55});
  fit::optional<T> c(T{42});
  fit::optional<T> d;
  fit::optional<T> e;

  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == d);
  EXPECT_TRUE(d == e);
  EXPECT_FALSE(d == a);

  EXPECT_FALSE(a == fit::nullopt);
  EXPECT_FALSE(fit::nullopt == a);
  EXPECT_TRUE(a == T{42});
  EXPECT_TRUE(T{42} == a);
  EXPECT_FALSE(a == T{55});
  EXPECT_FALSE(T{55} == a);
  EXPECT_FALSE(d == T{42});
  EXPECT_FALSE(T{42} == d);
  EXPECT_TRUE(d == fit::nullopt);
  EXPECT_TRUE(fit::nullopt == d);

  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a != c);
  EXPECT_TRUE(a != d);
  EXPECT_FALSE(d != e);
  EXPECT_TRUE(d != a);

  EXPECT_TRUE(a != fit::nullopt);
  EXPECT_TRUE(fit::nullopt != a);
  EXPECT_FALSE(a != T{42});
  EXPECT_FALSE(T{42} != a);
  EXPECT_TRUE(a != T{55});
  EXPECT_TRUE(T{55} != a);
  EXPECT_TRUE(d != T{42});
  EXPECT_TRUE(T{42} != d);
  EXPECT_FALSE(d != fit::nullopt);
  EXPECT_FALSE(fit::nullopt != d);
}

template <typename T>
void swapping() {
  fit::optional<T> a(T{42});
  fit::optional<T> b(T{55});
  fit::optional<T> c;
  fit::optional<T> d;

  swap(a, b);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);

  swap(a, c);
  EXPECT_FALSE(a.has_value());
  EXPECT_TRUE(c.has_value());
  EXPECT_EQ(55, c.value().value);

  swap(d, c);
  EXPECT_FALSE(c.has_value());
  EXPECT_TRUE(d.has_value());
  EXPECT_EQ(55, d.value().value);

  swap(c, a);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(a.has_value());

  swap(a, a);
  EXPECT_FALSE(a.has_value());

  swap(d, d);
  EXPECT_TRUE(d.has_value());
  EXPECT_EQ(55, d.value().value);
}

template <typename T>
void balance() {
  EXPECT_EQ(0, T::balance);
}

TEST(OptionalTests, make_optional) {
  {
    // Simple value.
    auto value = fit::make_optional<int>(10);
    static_assert(std::is_same<fit::optional<int>, decltype(value)>::value, "");
    EXPECT_EQ(*value, 10);
  }

  {
    // Multiple args.
    auto value = fit::make_optional<std::pair<int, int>>(10, 20);
    static_assert(std::is_same<fit::optional<std::pair<int, int>>, decltype(value)>::value, "");
    EXPECT_TRUE((*value == std::pair<int, int>{10, 20}));
  }

  {
    // Initializer list.
    auto value = fit::make_optional<std::vector<int>>({10, 20, 30});
    static_assert(std::is_same<fit::optional<std::vector<int>>, decltype(value)>::value, "");
    EXPECT_TRUE((*value == std::vector<int>{{10, 20, 30}}));
  }
}

}  // namespace

TEST(OptionalTests, construct_without_value_slot_false) { construct_without_value<slot<false>>(); }
TEST(OptionalTests, construct_without_value_slot_true) { construct_without_value<slot<true>>(); }
TEST(OptionalTests, construct_with_value_slot_false) { construct_with_value<slot<false>>(); }
TEST(OptionalTests, construct_with_value_slot_true) { construct_with_value<slot<true>>(); }
TEST(OptionalTests, construct_copy_slot_false) { construct_copy<slot<false>>(); }
TEST(OptionalTests, construct_copy_slot_true) { construct_copy<slot<true>>(); }
TEST(OptionalTests, construct_move_slot_false) { construct_move<slot<false>>(); }
TEST(OptionalTests, construct_move_slot_true) { construct_move<slot<true>>(); }
TEST(OptionalTests, accessors_slot_false) { accessors<slot<false>>(); }
TEST(OptionalTests, accessors_slot_true) { accessors<slot<true>>(); }
#if 0 || TEST_DOES_NOT_COMPILE
TEST(OptionalTests, assign_slot_false) { assign<slot<false>>(); }
#endif
TEST(OptionalTests, assign_slot_true) { assign<slot<true>>(); }
#if 0 || TEST_DOES_NOT_COMPILE
TEST(OptionalTests, assign_copy_slot_false) { assign_copy<slot<false>>(); }
#endif
TEST(OptionalTests, assign_copy_slot_true) { assign_copy<slot<true>>(); }
#if 0 || TEST_DOES_NOT_COMPILE
TEST(OptionalTests, assign_move_slot_false) { assign_move<slot<false>>(); }
#endif
TEST(OptionalTests, assign_move_slot_true) { assign_move<slot<true>>(); }
TEST(OptionalTests, emplace_slot_false) { emplace<slot<false>>(); }
TEST(OptionalTests, emplace_slot_true) { emplace<slot<true>>(); }
TEST(OptionalTests, invoke_slot_false) { invoke<slot<false>>(); }
TEST(OptionalTests, invoke_slot_true) { invoke<slot<true>>(); }
TEST(OptionalTests, comparisons_slot_false) { comparisons<slot<false>>(); }
TEST(OptionalTests, comparisons_slot_true) { comparisons<slot<true>>(); }
TEST(OptionalTests, swapping_slot_false) { swapping<slot<false>>(); }
TEST(OptionalTests, swapping_slot_true) { swapping<slot<true>>(); }
TEST(OptionalTests, balance_slot_false) { balance<slot<false>>(); }
TEST(OptionalTests, balance_slot_true) { balance<slot<true>>(); }
