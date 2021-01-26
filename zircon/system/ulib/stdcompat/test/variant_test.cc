// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/variant.h>

#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "test_helper.h"
namespace {

struct no_copy {
  no_copy(const no_copy&) = delete;
  no_copy(no_copy&&) = default;
  no_copy& operator=(const no_copy&) = delete;
  no_copy& operator=(no_copy&&) = default;
};

struct no_move {
  no_move(const no_move&) = default;
  no_move(no_move&&) = delete;
  no_move& operator=(const no_move&) = default;
  no_move& operator=(no_move&&) = delete;
};

struct no_copy_no_move {
  no_copy_no_move(const no_copy_no_move&) = delete;
  no_copy_no_move(no_copy_no_move&&) = delete;
  no_copy_no_move& operator=(const no_copy_no_move&) = delete;
  no_copy_no_move& operator=(no_copy_no_move&&) = delete;
};

struct non_trivial_destructor {
  ~non_trivial_destructor() {}
};

struct non_trivial_copy {
  non_trivial_copy(const non_trivial_copy&) {}
  non_trivial_copy(non_trivial_copy&&) = default;
  non_trivial_copy& operator=(const non_trivial_copy&) { return *this; }
  non_trivial_copy& operator=(non_trivial_copy&&) = default;
};

struct non_trivial_move {
  non_trivial_move(const non_trivial_move&) = default;
  non_trivial_move(non_trivial_move&&) {}
  non_trivial_move& operator=(const non_trivial_move&) = default;
  non_trivial_move& operator=(non_trivial_move&&) { return *this; }
};

struct literal_traits {
  using a_type = cpp17::monostate;
  using b_type = int;
  using c_type = double;
  using variant = cpp17::variant<a_type, b_type, c_type>;

  static constexpr a_type a_value{};
  static constexpr b_type b_value = 10;
  static constexpr c_type c_value = 2.5;
  static constexpr c_type c2_value = 4.2;

  static variant a, b, c;
  static constexpr variant const_a{};
  static constexpr variant const_b{cpp17::in_place_index<1>, b_value};
  static constexpr variant const_c{cpp17::in_place_index<2>, c_value};
};

literal_traits::variant literal_traits::a;
literal_traits::variant literal_traits::b{cpp17::in_place_index<1>, literal_traits::b_value};
literal_traits::variant literal_traits::c{cpp17::in_place_index<2>, literal_traits::c_value};

struct complex_traits {
  using a_type = cpp17::monostate;
  using b_type = int;
  using c_type = std::string;
  using variant = cpp17::variant<a_type, b_type, c_type>;

  static const a_type a_value;
  static const b_type b_value;
  static const c_type c_value;
  static const c_type c2_value;

  static variant a, b, c;
  static const variant const_a;
  static const variant const_b;
  static const variant const_c;
};

const cpp17::monostate complex_traits::a_value{};
const int complex_traits::b_value = 10;
const std::string complex_traits::c_value = "test";
const std::string complex_traits::c2_value = "another";

complex_traits::variant complex_traits::a;
complex_traits::variant complex_traits::b{cpp17::in_place_index<1>, complex_traits::b_value};
complex_traits::variant complex_traits::c{cpp17::in_place_index<2>, complex_traits::c_value};

const complex_traits::variant complex_traits::const_a;
const complex_traits::variant complex_traits::const_b{cpp17::in_place_index<1>,
                                                      complex_traits::b_value};
const complex_traits::variant complex_traits::const_c{cpp17::in_place_index<2>,
                                                      complex_traits::c_value};

template <typename T>
void accessors() {
  EXPECT_EQ(T::a.index(), 0u);
  EXPECT_EQ(T::a_value, cpp17::get<0>(T::a));
  EXPECT_EQ(T::a_value, cpp17::get<0>(T::const_a));

  EXPECT_EQ(T::b.index(), 1u);
  EXPECT_EQ(T::b_value, cpp17::get<1>(T::b));
  EXPECT_EQ(T::b_value, cpp17::get<1>(T::const_b));

  EXPECT_EQ(T::c.index(), 2u);
  EXPECT_EQ(T::c_value, cpp17::get<2>(T::c));
  EXPECT_EQ(T::c_value, cpp17::get<2>(T::const_c));
}

template <typename T>
void copy_move_assign() {
  using b_type = typename T::b_type;
  using c_type = typename T::c_type;

  typename T::variant x;
  EXPECT_EQ(0u, x.index());
  EXPECT_EQ(T::a_value, cpp17::get<0>(x));

  x = T::b;
  EXPECT_EQ(1u, x.index());
  EXPECT_TRUE(cpp17::holds_alternative<b_type>(x));
  EXPECT_FALSE(cpp17::holds_alternative<c_type>(x));
  EXPECT_EQ(T::b_value, cpp17::get<1>(x));

  x.template emplace<2>(T::c_value);
  EXPECT_EQ(2u, x.index());
  EXPECT_FALSE(cpp17::holds_alternative<b_type>(x));
  EXPECT_TRUE(cpp17::holds_alternative<c_type>(x));
  EXPECT_EQ(T::c_value, cpp17::get<2>(x));

  typename T::variant y(T::b);
  EXPECT_EQ(1u, y.index());
  EXPECT_EQ(T::b_value, cpp17::get<1>(y));

  x = std::move(y);
  EXPECT_EQ(1u, x.index());
  EXPECT_EQ(T::b_value, cpp17::get<1>(x));

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

  x = x;
  EXPECT_EQ(1u, x.index());
  EXPECT_EQ(T::b_value, cpp17::get<1>(x));

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  x = std::move(x);
  EXPECT_EQ(1u, x.index());
  EXPECT_TRUE(cpp17::holds_alternative<b_type>(x));
  EXPECT_FALSE(cpp17::holds_alternative<c_type>(x));
  EXPECT_EQ(T::b_value, cpp17::get<1>(x));

  x = T::a;
  EXPECT_EQ(0u, x.index());
  EXPECT_EQ(T::a_value, cpp17::get<0>(x));

  x = T::c;
  typename T::variant z(std::move(x));
  EXPECT_EQ(2u, z.index());
  EXPECT_FALSE(cpp17::holds_alternative<b_type>(z));
  EXPECT_TRUE(cpp17::holds_alternative<c_type>(z));
  EXPECT_EQ(T::c_value, cpp17::get<2>(z));
}

template <typename T>
void swapping() {
  typename T::variant x;
  EXPECT_EQ(0u, x.index());
  EXPECT_EQ(T::a_value, cpp17::get<0>(x));

  typename T::variant y(T::c);
  y.swap(y);
  EXPECT_EQ(2u, y.index());
  EXPECT_EQ(T::c_value, cpp17::get<2>(y));

  x.swap(y);
  EXPECT_EQ(2u, x.index());
  EXPECT_EQ(T::c_value, cpp17::get<2>(x));
  EXPECT_EQ(0u, y.index());
  EXPECT_EQ(T::a_value, cpp17::get<0>(y));

  y.template emplace<2>(T::c2_value);
  x.swap(y);
  EXPECT_EQ(2u, x.index());
  EXPECT_EQ(T::c2_value, cpp17::get<2>(x));
  EXPECT_EQ(2u, y.index());
  EXPECT_EQ(T::c_value, cpp17::get<2>(y));

  x = T::b;
  y.swap(x);
  EXPECT_EQ(2u, x.index());
  EXPECT_EQ(T::c_value, cpp17::get<2>(x));
  EXPECT_EQ(1u, y.index());
  EXPECT_EQ(T::b_value, cpp17::get<1>(y));

  x = T::a;
  y.swap(x);
  EXPECT_EQ(1u, x.index());
  EXPECT_EQ(T::b_value, cpp17::get<1>(x));
  EXPECT_EQ(0u, y.index());
  EXPECT_EQ(T::a_value, cpp17::get<0>(y));
}

template <typename T>
void get_wrong_type() {
  ASSERT_THROW_OR_ABORT(
      {
        using b_type = typename T::b_type;
        typename T::variant x;
        EXPECT_EQ(0u, x.index());
        cpp17::get<b_type>(x);
      },
      cpp17::bad_variant_access);
}

template <typename T>
void get_wrong_index() {
  ASSERT_THROW_OR_ABORT(
      {
        typename T::variant x;
        EXPECT_EQ(0u, x.index());
        cpp17::get<1>(x);
      },
      cpp17::bad_variant_access);
}

// Test constexpr behavior.
namespace constexpr_test {
static_assert(literal_traits::variant().index() == 0, "");
static_assert(literal_traits::const_a.index() == 0, "");
static_assert(cpp17::get<0>(literal_traits::const_a) == literal_traits::a_value, "");
static_assert(literal_traits::const_b.index() == 1, "");
static_assert(cpp17::get<1>(literal_traits::const_b) == literal_traits::b_value, "");
static_assert(literal_traits::const_c.index() == 2, "");
static_assert(cpp17::get<2>(literal_traits::const_c) == literal_traits::c_value, "");
}  // namespace constexpr_test

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

// These definitions make cpp17::monostate always compare less than
// |less| and |greater|.
constexpr bool operator==(cpp17::monostate, greater) { return false; }
constexpr bool operator<=(cpp17::monostate, greater) { return true; }
constexpr bool operator>=(cpp17::monostate, greater) { return false; }
constexpr bool operator!=(cpp17::monostate, greater) { return true; }
constexpr bool operator<(cpp17::monostate, greater) { return true; }
constexpr bool operator>(cpp17::monostate, greater) { return false; }

constexpr bool operator==(greater, cpp17::monostate) { return false; }
constexpr bool operator<=(greater, cpp17::monostate) { return false; }
constexpr bool operator>=(greater, cpp17::monostate) { return true; }
constexpr bool operator!=(greater, cpp17::monostate) { return true; }
constexpr bool operator<(greater, cpp17::monostate) { return false; }
constexpr bool operator>(greater, cpp17::monostate) { return true; }

constexpr bool operator==(cpp17::monostate, less) { return false; }
constexpr bool operator<=(cpp17::monostate, less) { return true; }
constexpr bool operator>=(cpp17::monostate, less) { return false; }
constexpr bool operator!=(cpp17::monostate, less) { return true; }
constexpr bool operator<(cpp17::monostate, less) { return true; }
constexpr bool operator>(cpp17::monostate, less) { return false; }

constexpr bool operator==(less, cpp17::monostate) { return false; }
constexpr bool operator<=(less, cpp17::monostate) { return false; }
constexpr bool operator>=(less, cpp17::monostate) { return true; }
constexpr bool operator!=(less, cpp17::monostate) { return true; }
constexpr bool operator<(less, cpp17::monostate) { return false; }
constexpr bool operator>(less, cpp17::monostate) { return true; }

template <typename T, typename U>
constexpr bool match_comparisons(T lhs, U rhs) {
  // Use the following variant for all of the tests below. Note that the types
  // are ordered such that unlike variant comparisons yield a total order.
  // That is: cpp17::monostate < less < greater.
  using variant = cpp17::variant<cpp17::monostate, less, greater>;

  static_assert((variant{lhs} == variant{rhs}) == (lhs == rhs), "");
  static_assert((variant{lhs} != variant{rhs}) == (lhs != rhs), "");
  static_assert((variant{lhs} <= variant{rhs}) == (lhs <= rhs), "");
  static_assert((variant{lhs} >= variant{rhs}) == (lhs >= rhs), "");
  static_assert((variant{lhs} < variant{rhs}) == (lhs < rhs), "");
  static_assert((variant{lhs} > variant{rhs}) == (lhs > rhs), "");

  return true;
}

static_assert(match_comparisons(cpp17::monostate{}, cpp17::monostate{}), "");
static_assert(match_comparisons(cpp17::monostate{}, less{}), "");
static_assert(match_comparisons(cpp17::monostate{}, greater{}), "");
static_assert(match_comparisons(less{}, cpp17::monostate{}), "");
static_assert(match_comparisons(less{}, less{}), "");
static_assert(match_comparisons(less{}, greater{}), "");
static_assert(match_comparisons(greater{}, cpp17::monostate{}), "");
static_assert(match_comparisons(greater{}, less{}), "");
static_assert(match_comparisons(greater{}, greater{}), "");

}  // namespace comparison_tests

// Ensure the variant is copy-constructible only when the types are copyable.
namespace copy_construction_test {
static_assert(std::is_copy_constructible<cpp17::variant<cpp17::monostate>>::value, "");
static_assert(!std::is_copy_constructible<cpp17::variant<cpp17::monostate, no_copy>>::value, "");
static_assert(std::is_copy_constructible<cpp17::variant<cpp17::monostate, no_move>>::value, "");
static_assert(!std::is_copy_constructible<cpp17::variant<cpp17::monostate, no_copy_no_move>>::value,
              "");
static_assert(std::is_copy_constructible<literal_traits::variant>::value, "");
static_assert(std::is_copy_constructible<complex_traits::variant>::value, "");
}  // namespace copy_construction_test

// Ensure the variant is copy-assignable only when the types are copyable.
namespace copy_assignment_test {
static_assert(std::is_copy_assignable<cpp17::variant<cpp17::monostate>>::value, "");
static_assert(!std::is_copy_assignable<cpp17::variant<cpp17::monostate, no_copy>>::value, "");
static_assert(std::is_copy_assignable<cpp17::variant<cpp17::monostate, no_move>>::value, "");
static_assert(!std::is_copy_assignable<cpp17::variant<cpp17::monostate, no_copy_no_move>>::value,
              "");
static_assert(std::is_copy_assignable<literal_traits::variant>::value, "");
static_assert(std::is_copy_assignable<complex_traits::variant>::value, "");
}  // namespace copy_assignment_test

// Ensure the variant is move-constructible only when the types are movable.
// Note that copy-constructible types are also considered movable.
namespace move_construction_test {
static_assert(std::is_move_constructible<cpp17::variant<cpp17::monostate>>::value, "");
static_assert(std::is_move_constructible<cpp17::variant<cpp17::monostate, no_copy>>::value, "");
static_assert(std::is_move_constructible<cpp17::variant<cpp17::monostate, no_move>>::value, "");
static_assert(!std::is_move_constructible<cpp17::variant<cpp17::monostate, no_copy_no_move>>::value,
              "");
static_assert(std::is_move_constructible<literal_traits::variant>::value, "");
static_assert(std::is_move_constructible<complex_traits::variant>::value, "");
}  // namespace move_construction_test

// Ensure the variant is move-assignable only when the types are movable.
// Note that copy-assignable types are also considered movable.
namespace move_assignment_test {
static_assert(std::is_move_assignable<cpp17::variant<cpp17::monostate>>::value, "");
static_assert(std::is_move_assignable<cpp17::variant<cpp17::monostate, no_copy>>::value, "");
static_assert(std::is_move_assignable<cpp17::variant<cpp17::monostate, no_move>>::value, "");
static_assert(!std::is_move_assignable<cpp17::variant<cpp17::monostate, no_copy_no_move>>::value,
              "");
static_assert(std::is_move_assignable<literal_traits::variant>::value, "");
static_assert(std::is_move_assignable<complex_traits::variant>::value, "");
}  // namespace move_assignment_test

// Ensure that the correct sequence of base types are considered in the
// implementation of variant to ensure that the right methods participate
// in overload resolution.
namespace impl_test {

// Type with a trivial destructor, move, and copy.
namespace trivial_type {
static_assert(std::is_trivially_destructible<cpp17::variant<cpp17::monostate, int>>::value, "");
static_assert(std::is_trivially_move_constructible<cpp17::variant<cpp17::monostate, int>>::value,
              "");
static_assert(std::is_trivially_copy_constructible<cpp17::variant<cpp17::monostate, int>>::value,
              "");
static_assert(std::is_trivially_move_assignable<cpp17::variant<cpp17::monostate, int>>::value, "");
static_assert(std::is_trivially_copy_assignable<cpp17::variant<cpp17::monostate, int>>::value, "");
}  // namespace trivial_type

// Type with a non-trivial destructor implies it has non-trivial move and copy too.
namespace non_trivial_destructor_type {
static_assert(!std::is_trivially_destructible<
                  cpp17::variant<cpp17::monostate, non_trivial_destructor>>::value,
              "");
static_assert(!std::is_trivially_move_constructible<
                  cpp17::variant<cpp17::monostate, non_trivial_destructor>>::value,
              "");
static_assert(!std::is_trivially_copy_constructible<
                  cpp17::variant<cpp17::monostate, non_trivial_destructor>>::value,
              "");
static_assert(!std::is_trivially_move_assignable<
                  cpp17::variant<cpp17::monostate, non_trivial_destructor>>::value,
              "");
static_assert(!std::is_trivially_copy_assignable<
                  cpp17::variant<cpp17::monostate, non_trivial_destructor>>::value,
              "");
}  // namespace non_trivial_destructor_type

// Type with a non-trivial move constructor actually ends up being trivially
// movable anyhow if it has a trivial copy constructor and destructor.
namespace non_trivial_move_type {
static_assert(
    std::is_trivially_destructible<cpp17::variant<cpp17::monostate, non_trivial_move>>::value, "");
static_assert(!std::is_trivially_move_constructible<
                  cpp17::variant<cpp17::monostate, non_trivial_move>>::value,
              "");
static_assert(
    std::is_trivially_copy_constructible<cpp17::variant<cpp17::monostate, non_trivial_move>>::value,
    "");
static_assert(
    !std::is_trivially_move_assignable<cpp17::variant<cpp17::monostate, non_trivial_move>>::value,
    "");
static_assert(
    std::is_trivially_copy_assignable<cpp17::variant<cpp17::monostate, non_trivial_move>>::value,
    "");
}  // namespace non_trivial_move_type

// Type with a non-trivial copy constructor may be trivially movable while not
// trivially copyable.
namespace non_trivial_copy_type {
static_assert(
    std::is_trivially_destructible<cpp17::variant<cpp17::monostate, non_trivial_copy>>::value, "");
static_assert(
    std::is_trivially_move_constructible<cpp17::variant<cpp17::monostate, non_trivial_copy>>::value,
    "");
static_assert(!std::is_trivially_copy_constructible<
                  cpp17::variant<cpp17::monostate, non_trivial_copy>>::value,
              "");
static_assert(
    std::is_trivially_move_assignable<cpp17::variant<cpp17::monostate, non_trivial_copy>>::value,
    "");
static_assert(
    !std::is_trivially_copy_assignable<cpp17::variant<cpp17::monostate, non_trivial_copy>>::value,
    "");
}  // namespace non_trivial_copy_type

// std::string is not trivally destructible, movable, or copyable.
namespace string_type {
static_assert(!std::is_trivially_destructible<cpp17::variant<cpp17::monostate, std::string>>::value,
              "");
static_assert(
    !std::is_trivially_move_constructible<cpp17::variant<cpp17::monostate, std::string>>::value,
    "");
static_assert(
    !std::is_trivially_copy_constructible<cpp17::variant<cpp17::monostate, std::string>>::value,
    "");
static_assert(
    !std::is_trivially_move_assignable<cpp17::variant<cpp17::monostate, std::string>>::value, "");
static_assert(
    !std::is_trivially_copy_assignable<cpp17::variant<cpp17::monostate, std::string>>::value, "");
}  // namespace string_type
}  // namespace impl_test

TEST(VariantTest, AccessorsOnLiterals) { accessors<literal_traits>(); }
TEST(VariantTest, AccessorsOnComplex) { accessors<complex_traits>(); }
TEST(VariantTest, CopyMoveAssignWithLiteral) { copy_move_assign<literal_traits>(); }
TEST(VariantTest, CopyMoveAssignWithComplex) { copy_move_assign<complex_traits>(); }
TEST(VariantTest, SwappingWithLiteral) { swapping<literal_traits>(); }
TEST(VariantTest, SwappingWithComplex) { swapping<complex_traits>(); }
TEST(VariantTest, GetWrongTypeAbortsLiteral) { get_wrong_type<literal_traits>(); }
TEST(VariantTest, GetWrongTypeAbortsComplex) { get_wrong_type<complex_traits>(); }
TEST(VariantTest, GetWrongIndexAbortsLiteral) { get_wrong_index<literal_traits>(); }
TEST(VariantTest, GetWrongIndexAbortsComplex) { get_wrong_index<complex_traits>(); }

#if __cpp_lib_variant >= 201606L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

// Sanity check that the template switches correctly.
TEST(VariantTest, PolyfillIsAliasWhenVariantIsAvailable) {
  static_assert(std::is_same_v<std::variant<int, float>, cpp17::variant<int, float>>);
}
#endif

}  // namespace
