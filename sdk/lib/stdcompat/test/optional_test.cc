// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/optional.h>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "test_helper.h"

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
    EXPECT_GT(balance, 0);
    EXPECT_NE(value, -1);
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
static_assert(std::is_same<int, cpp17::optional<int>::value_type>::value, "");

// Test basic constexpr context.
static_assert(cpp17::optional<int>{}.has_value() == false, "");
static_assert(cpp17::optional<int>{10}.has_value() == true, "");
static_assert(cpp17::optional<int>{10U}.has_value() == true, "");

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
// empty-to-empty comparison behavior of cpp17::optional for convenience in
// exhaustive testing.
constexpr bool operator==(cpp17::nullopt_t, greater) { return false; }
constexpr bool operator<=(cpp17::nullopt_t, greater) { return true; }
constexpr bool operator>=(cpp17::nullopt_t, greater) { return false; }
constexpr bool operator!=(cpp17::nullopt_t, greater) { return true; }
constexpr bool operator<(cpp17::nullopt_t, greater) { return true; }
constexpr bool operator>(cpp17::nullopt_t, greater) { return false; }

constexpr bool operator==(greater, cpp17::nullopt_t) { return false; }
constexpr bool operator<=(greater, cpp17::nullopt_t) { return false; }
constexpr bool operator>=(greater, cpp17::nullopt_t) { return true; }
constexpr bool operator!=(greater, cpp17::nullopt_t) { return true; }
constexpr bool operator<(greater, cpp17::nullopt_t) { return false; }
constexpr bool operator>(greater, cpp17::nullopt_t) { return true; }

constexpr bool operator==(cpp17::nullopt_t, less) { return false; }
constexpr bool operator<=(cpp17::nullopt_t, less) { return true; }
constexpr bool operator>=(cpp17::nullopt_t, less) { return false; }
constexpr bool operator!=(cpp17::nullopt_t, less) { return true; }
constexpr bool operator<(cpp17::nullopt_t, less) { return true; }
constexpr bool operator>(cpp17::nullopt_t, less) { return false; }

constexpr bool operator==(less, cpp17::nullopt_t) { return false; }
constexpr bool operator<=(less, cpp17::nullopt_t) { return false; }
constexpr bool operator>=(less, cpp17::nullopt_t) { return true; }
constexpr bool operator!=(less, cpp17::nullopt_t) { return true; }
constexpr bool operator<(less, cpp17::nullopt_t) { return false; }
constexpr bool operator>(less, cpp17::nullopt_t) { return true; }

constexpr bool operator==(cpp17::nullopt_t, cpp17::nullopt_t) { return true; }
constexpr bool operator<=(cpp17::nullopt_t, cpp17::nullopt_t) { return true; }
constexpr bool operator>=(cpp17::nullopt_t, cpp17::nullopt_t) { return true; }
constexpr bool operator!=(cpp17::nullopt_t, cpp17::nullopt_t) { return false; }
constexpr bool operator<(cpp17::nullopt_t, cpp17::nullopt_t) { return false; }
constexpr bool operator>(cpp17::nullopt_t, cpp17::nullopt_t) { return false; }

template <typename T, typename U>
constexpr bool match_comparisons(T lhs, U rhs) {
  // Both optional operands.
  static_assert((cpp17::optional<T>{lhs} == cpp17::optional<U>{rhs}) == (lhs == rhs), "");
  static_assert((cpp17::optional<T>{lhs} != cpp17::optional<U>{rhs}) == (lhs != rhs), "");
  static_assert((cpp17::optional<T>{lhs} <= cpp17::optional<U>{rhs}) == (lhs <= rhs), "");
  static_assert((cpp17::optional<T>{lhs} >= cpp17::optional<U>{rhs}) == (lhs >= rhs), "");
  static_assert((cpp17::optional<T>{lhs} < cpp17::optional<U>{rhs}) == (lhs < rhs), "");
  static_assert((cpp17::optional<T>{lhs} > cpp17::optional<U>{rhs}) == (lhs > rhs), "");

  static_assert((cpp17::optional<T>{} == cpp17::optional<U>{rhs}) == (cpp17::nullopt == rhs), "");
  static_assert((cpp17::optional<T>{} != cpp17::optional<U>{rhs}) == (cpp17::nullopt != rhs), "");
  static_assert((cpp17::optional<T>{} <= cpp17::optional<U>{rhs}) == (cpp17::nullopt <= rhs), "");
  static_assert((cpp17::optional<T>{} >= cpp17::optional<U>{rhs}) == (cpp17::nullopt >= rhs), "");
  static_assert((cpp17::optional<T>{} < cpp17::optional<U>{rhs}) == (cpp17::nullopt < rhs), "");
  static_assert((cpp17::optional<T>{} > cpp17::optional<U>{rhs}) == (cpp17::nullopt > rhs), "");

  static_assert((cpp17::optional<T>{lhs} == cpp17::optional<U>{}) == (lhs == cpp17::nullopt), "");
  static_assert((cpp17::optional<T>{lhs} != cpp17::optional<U>{}) == (lhs != cpp17::nullopt), "");
  static_assert((cpp17::optional<T>{lhs} <= cpp17::optional<U>{}) == (lhs <= cpp17::nullopt), "");
  static_assert((cpp17::optional<T>{lhs} >= cpp17::optional<U>{}) == (lhs >= cpp17::nullopt), "");
  static_assert((cpp17::optional<T>{lhs} < cpp17::optional<U>{}) == (lhs < cpp17::nullopt), "");
  static_assert((cpp17::optional<T>{lhs} > cpp17::optional<U>{}) == (lhs > cpp17::nullopt), "");

  static_assert(
      (cpp17::optional<T>{} == cpp17::optional<U>{}) == (cpp17::nullopt == cpp17::nullopt), "");
  static_assert(
      (cpp17::optional<T>{} != cpp17::optional<U>{}) == (cpp17::nullopt != cpp17::nullopt), "");
  static_assert(
      (cpp17::optional<T>{} <= cpp17::optional<U>{}) == (cpp17::nullopt <= cpp17::nullopt), "");
  static_assert(
      (cpp17::optional<T>{} >= cpp17::optional<U>{}) == (cpp17::nullopt >= cpp17::nullopt), "");
  static_assert((cpp17::optional<T>{} < cpp17::optional<U>{}) == (cpp17::nullopt < cpp17::nullopt),
                "");
  static_assert((cpp17::optional<T>{} > cpp17::optional<U>{}) == (cpp17::nullopt > cpp17::nullopt),
                "");

  // Right hand optional only.
  static_assert((lhs == cpp17::optional<U>{rhs}) == (lhs == rhs), "");
  static_assert((lhs != cpp17::optional<U>{rhs}) == (lhs != rhs), "");
  static_assert((lhs <= cpp17::optional<U>{rhs}) == (lhs <= rhs), "");
  static_assert((lhs >= cpp17::optional<U>{rhs}) == (lhs >= rhs), "");
  static_assert((lhs < cpp17::optional<U>{rhs}) == (lhs < rhs), "");
  static_assert((lhs > cpp17::optional<U>{rhs}) == (lhs > rhs), "");

  static_assert((lhs == cpp17::optional<U>{}) == (lhs == cpp17::nullopt), "");
  static_assert((lhs != cpp17::optional<U>{}) == (lhs != cpp17::nullopt), "");
  static_assert((lhs <= cpp17::optional<U>{}) == (lhs <= cpp17::nullopt), "");
  static_assert((lhs >= cpp17::optional<U>{}) == (lhs >= cpp17::nullopt), "");
  static_assert((lhs < cpp17::optional<U>{}) == (lhs < cpp17::nullopt), "");
  static_assert((lhs > cpp17::optional<U>{}) == (lhs > cpp17::nullopt), "");

  // Left hand optional only.
  static_assert((cpp17::optional<T>{lhs} == rhs) == (lhs == rhs), "");
  static_assert((cpp17::optional<T>{lhs} != rhs) == (lhs != rhs), "");
  static_assert((cpp17::optional<T>{lhs} <= rhs) == (lhs <= rhs), "");
  static_assert((cpp17::optional<T>{lhs} >= rhs) == (lhs >= rhs), "");
  static_assert((cpp17::optional<T>{lhs} < rhs) == (lhs < rhs), "");
  static_assert((cpp17::optional<T>{lhs} > rhs) == (lhs > rhs), "");

  static_assert((cpp17::optional<T>{} == rhs) == (cpp17::nullopt == rhs), "");
  static_assert((cpp17::optional<T>{} != rhs) == (cpp17::nullopt != rhs), "");
  static_assert((cpp17::optional<T>{} <= rhs) == (cpp17::nullopt <= rhs), "");
  static_assert((cpp17::optional<T>{} >= rhs) == (cpp17::nullopt >= rhs), "");
  static_assert((cpp17::optional<T>{} < rhs) == (cpp17::nullopt < rhs), "");
  static_assert((cpp17::optional<T>{} > rhs) == (cpp17::nullopt > rhs), "");

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
  constexpr trivially_move_only() = default;

  constexpr trivially_move_only(const trivially_move_only&) = delete;
  constexpr trivially_move_only& operator=(const trivially_move_only&) = delete;

  constexpr trivially_move_only(trivially_move_only&&) = default;
  constexpr trivially_move_only& operator=(trivially_move_only&&) = default;

  int value = 0;
};

static_assert(std::is_trivially_copy_constructible<trivially_move_only>::value == false);
static_assert(std::is_trivially_copy_assignable<trivially_move_only>::value == false);
static_assert(std::is_trivially_move_constructible<trivially_move_only>::value == true);
static_assert(std::is_trivially_move_assignable<trivially_move_only>::value == true);

static_assert(std::is_trivially_copy_constructible<cpp17::optional<trivially_move_only>>::value ==
              false);
static_assert(std::is_trivially_copy_assignable<cpp17::optional<trivially_move_only>>::value ==
              false);
static_assert(std::is_trivially_move_constructible<cpp17::optional<trivially_move_only>>::value ==
              true);
static_assert(std::is_trivially_move_assignable<cpp17::optional<trivially_move_only>>::value ==
              true);

struct trivially_copyable {
  constexpr trivially_copyable() = default;

  constexpr trivially_copyable(const trivially_copyable&) = default;
  constexpr trivially_copyable& operator=(const trivially_copyable&) = default;

  int value = 0;
};

static_assert(std::is_trivially_copy_constructible<trivially_copyable>::value == true);
static_assert(std::is_trivially_copy_assignable<trivially_copyable>::value == true);
static_assert(std::is_trivially_move_constructible<trivially_copyable>::value == true);
static_assert(std::is_trivially_move_assignable<trivially_copyable>::value == true);

static_assert(std::is_trivially_copy_constructible<cpp17::optional<trivially_copyable>>::value ==
              true);
static_assert(std::is_trivially_copy_assignable<cpp17::optional<trivially_copyable>>::value ==
              true);
static_assert(std::is_trivially_move_constructible<cpp17::optional<trivially_copyable>>::value ==
              true);
static_assert(std::is_trivially_move_assignable<cpp17::optional<trivially_copyable>>::value ==
              true);

}  // namespace trivial_copy_move_tests

template <typename T>
void construct_without_value() {
  cpp17::optional<T> opt;
  EXPECT_FALSE(opt.has_value());
  EXPECT_FALSE(!!opt);

  EXPECT_EQ(42, opt.value_or(T{42}).value);

  opt.reset();
  EXPECT_FALSE(opt.has_value());
}

template <typename T>
void construct_with_value() {
  cpp17::optional<T> opt(T{42});
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
  cpp17::optional<T> a(T{42});
  cpp17::optional<T> b(a);
  cpp17::optional<T> c;
  cpp17::optional<T> d(c);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(d.has_value());
}

template <typename T>
void construct_move() {
  cpp17::optional<T> a(T{42});
  cpp17::optional<T> b(std::move(a));
  cpp17::optional<T> c;
  cpp17::optional<T> d(std::move(c));
  EXPECT_TRUE(a.has_value());
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(d.has_value());
}

template <typename T>
T get_value(cpp17::optional<T> opt) {
  return opt.value();
}

TEST(OptionalTests, construct_with_implicit_conversion) {
  // get_value expects a value of type cpp17::optional<T> but we pass 3
  // so this exercises the converting constructor
  EXPECT_EQ(3, get_value<int>(3));
}

template <typename T>
void accessors() {
  cpp17::optional<T> a(T{42});
  T& value = a.value();
  EXPECT_EQ(42, value.value);

  const T& const_value = const_cast<const decltype(a)&>(a).value();
  EXPECT_EQ(42, const_value.value);

  T rvalue = cpp17::optional<T>(T{42}).value();
  EXPECT_EQ(42, rvalue.value);

  T const_rvalue = const_cast<const cpp17::optional<T>&&>(cpp17::optional<T>(T{42})).value();
  EXPECT_EQ(42, const_rvalue.value);
}

template <typename T>
void assign() {
  cpp17::optional<T> a(T{42});
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

  a = cpp17::nullopt;
  EXPECT_FALSE(a.has_value());
}

template <typename T>
void assign_copy() {
  cpp17::optional<T> a(T{42});
  cpp17::optional<T> b(T{55});
  cpp17::optional<T> c;
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
  cpp17::optional<T> a(T{42});
  cpp17::optional<T> b(T{55});
  cpp17::optional<T> c;
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
  cpp17::optional<T> a;
  EXPECT_EQ(55, a.emplace(55).value);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);

  cpp17::optional<T> b(T{42});
  EXPECT_EQ(66, b.emplace(66).value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(66, b.value().value);
}

template <typename T>
void invoke() {
  cpp17::optional<T> a(T{42});
  EXPECT_EQ(42, a->get());
  EXPECT_EQ(43, a->increment());
  EXPECT_EQ(43, (*a).value);
}

template <typename T>
void comparisons() {
  cpp17::optional<T> a(T{42});
  cpp17::optional<T> b(T{55});
  cpp17::optional<T> c(T{42});
  cpp17::optional<T> d;
  cpp17::optional<T> e;

  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == d);
  EXPECT_TRUE(d == e);
  EXPECT_FALSE(d == a);

  EXPECT_FALSE(a == cpp17::nullopt);
  EXPECT_FALSE(cpp17::nullopt == a);
  EXPECT_TRUE(a == T{42});
  EXPECT_TRUE(T{42} == a);
  EXPECT_FALSE(a == T{55});
  EXPECT_FALSE(T{55} == a);
  EXPECT_FALSE(d == T{42});
  EXPECT_FALSE(T{42} == d);
  EXPECT_TRUE(d == cpp17::nullopt);
  EXPECT_TRUE(cpp17::nullopt == d);

  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a != c);
  EXPECT_TRUE(a != d);
  EXPECT_FALSE(d != e);
  EXPECT_TRUE(d != a);

  EXPECT_TRUE(a != cpp17::nullopt);
  EXPECT_TRUE(cpp17::nullopt != a);
  EXPECT_FALSE(a != T{42});
  EXPECT_FALSE(T{42} != a);
  EXPECT_TRUE(a != T{55});
  EXPECT_TRUE(T{55} != a);
  EXPECT_TRUE(d != T{42});
  EXPECT_TRUE(T{42} != d);
  EXPECT_FALSE(d != cpp17::nullopt);
  EXPECT_FALSE(cpp17::nullopt != d);
}

template <typename T>
void swapping() {
  cpp17::optional<T> a(T{42});
  cpp17::optional<T> b(T{55});
  cpp17::optional<T> c;
  cpp17::optional<T> d;

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
    auto value = cpp17::make_optional<int>(10);
    static_assert(std::is_same<cpp17::optional<int>, decltype(value)>::value, "");
    EXPECT_EQ(*value, 10);
  }

  {
    // Multiple args.
    auto value = cpp17::make_optional<std::pair<int, int>>(10, 20);
    static_assert(std::is_same<cpp17::optional<std::pair<int, int>>, decltype(value)>::value, "");
    EXPECT_TRUE((*value == std::pair<int, int>{10, 20}));
  }

  {
    // Initializer list.
    auto value = cpp17::make_optional<std::vector<int>>({10, 20, 30});
    static_assert(std::is_same<cpp17::optional<std::vector<int>>, decltype(value)>::value, "");
    EXPECT_TRUE((*value == std::vector<int>{{10, 20, 30}}));
  }
}

TEST(OptionalTest, ConstructWithoutValueAndNoAssignmentOperators) {
  construct_without_value<slot<false>>();
}

TEST(OptionalTest, ConstructWithoutValueAndWithAssignmentOperators) {
  construct_without_value<slot<true>>();
}

TEST(OptionalTest, ConstructWithValueAndNoAssignmentOperators) {
  construct_with_value<slot<false>>();
}

TEST(OptionalTest, ConstructWithValueAndAssignmentOperators) { construct_with_value<slot<true>>(); }
TEST(OptionalTest, ConstructWithCopyAndNoAssignmentOperators) { construct_copy<slot<false>>(); }
TEST(OptionalTest, ConstructWithCopyAndAssignmentOperators) { construct_copy<slot<true>>(); }
TEST(OptionalTest, ConstructWithMoveAndNoAssignementOperators) { construct_move<slot<false>>(); }
TEST(OptionalTest, ConstructWithMoveAndAssignmentOperators) { construct_move<slot<true>>(); }
TEST(OptionalTest, AccessorsWithoutAssignmentOperators) { accessors<slot<false>>(); }
TEST(OptionalTest, AccessorsWithSlotTrue) { accessors<slot<true>>(); }
TEST(OptionalTest, AssignWithAssignmentOperators) { assign<slot<true>>(); }
TEST(OptionalTest, AssignWithCopyAndAssignmentOperators) { assign_copy<slot<true>>(); }
TEST(OptionalTest, AssignWithMoveAndAssignmentOperators) { assign_move<slot<true>>(); }
TEST(OptionalTest, EmplaceWithNoAssignmentOperators) { emplace<slot<false>>(); }
TEST(OptionalTest, EmplaceWithAssignmentOperators) { emplace<slot<true>>(); }
TEST(OptionalTest, InvokeWithNoAssignmentOperators) { invoke<slot<false>>(); }
TEST(OptionalTest, InvokeWithAssignmentOperators) { invoke<slot<true>>(); }
TEST(OptionalTest, ComparisonWithNoAssignmentOperators) { comparisons<slot<false>>(); }
TEST(OptionalTest, ComparisonWithAssignmentOperators) { comparisons<slot<true>>(); }
TEST(OptionalTest, SwappingWithoutAssignmentOperators) { swapping<slot<false>>(); }
TEST(OptionalTest, SwappingWithAssignmentOperators) { swapping<slot<true>>(); }

TEST(OptionalTest, DestructorCalledWhenNotEmpty) {
  balance<slot<false>>();
  balance<slot<true>>();
}

// When exceptions are disabled, this defaults to __builtin_abort.
TEST(OptionalTest, MutableRefValueAccessorWhenEmptyIsBadOptionalAccess) {
  ASSERT_THROW_OR_ABORT(
      {
        cpp17::optional<int> empty_optional;
        int& j [[gnu::unused]] = empty_optional.value();
      },
      cpp17::bad_optional_access);
}

TEST(OptionalTest, ConstRefValueAccessorWhenEmptyIsBadOptionalAccess2) {
  ASSERT_THROW_OR_ABORT(
      {
        cpp17::optional<int> empty_optional;
        const int& j [[gnu::unused]] = empty_optional.value();
      },
      cpp17::bad_optional_access);
}

TEST(OptionalTest, MutableRValueValueAccessorWhenEmptyIsBadOptionalAccess3) {
  ASSERT_THROW_OR_ABORT(
      {
        cpp17::optional<int> empty_optional;
        int j [[gnu::unused]] = std::move(empty_optional).value();
      },
      cpp17::bad_optional_access);
}

[[gnu::unused]] int get_value(const std::optional<int>&& a) { return a.value(); }

TEST(OptionalTest, ConstRvalueValueAccessorWhenEmptyIsBadOptionalAccess4) {
  ASSERT_THROW_OR_ABORT(
      {
        cpp17::optional<int> empty_optional;
        const int b [[gnu::unused]] = get_value(empty_optional);
      },
      cpp17::bad_optional_access);
}

#if __cpp_lib_optional >= 201606L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

// Sanity check that the template switches correctly.
TEST(OptionalTest, PolyfillIsAliasWhenOptionaIsAvailable) {
  static_assert(std::is_same_v<std::optional<int>, cpp17::optional<int>>);
  static_assert(std::is_same_v<std::optional<std::string>, cpp17::optional<std::string>>);
  static_assert(
      std::is_same_v<std::optional<std::unique_ptr<int>>, cpp17::optional<std::unique_ptr<int>>>);
}
#endif

#if defined(TEST_DOES_NOT_COMPILE)
TEST(OptionalTests, assign_copy_slot_false) { assign_copy<slot<false>>(); }
TEST(OptionalTests, assign_slot_false) { assign<slot<false>>(); }
TEST(OptionalTests, assign_move_slot_false) { assign_move<slot<false>>(); }
#endif

}  // namespace
