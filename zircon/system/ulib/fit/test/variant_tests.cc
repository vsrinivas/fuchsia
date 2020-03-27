// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <type_traits>

#include <lib/fit/variant.h>
#include <unittest/unittest.h>

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
  using variant = fit::variant<fit::monostate, int, double>;

  static constexpr fit::monostate a_value{};
  static constexpr int b_value = 10;
  static constexpr double c_value = 2.5;
  static constexpr double c2_value = 4.2;

  static variant a, b, c;
  static constexpr variant const_a{};
  static constexpr variant const_b{fit::in_place_index<1>, b_value};
  static constexpr variant const_c{fit::in_place_index<2>, c_value};
};

literal_traits::variant literal_traits::a;
literal_traits::variant literal_traits::b{fit::in_place_index<1>, literal_traits::b_value};
literal_traits::variant literal_traits::c{fit::in_place_index<2>, literal_traits::c_value};

struct complex_traits {
  using variant = fit::variant<fit::monostate, int, std::string>;

  static const fit::monostate a_value;
  static const int b_value;
  static const std::string c_value;
  static const std::string c2_value;

  static variant a, b, c;
  static const variant const_a;
  static const variant const_b;
  static const variant const_c;
};

const fit::monostate complex_traits::a_value{};
const int complex_traits::b_value = 10;
const std::string complex_traits::c_value = "test";
const std::string complex_traits::c2_value = "another";

complex_traits::variant complex_traits::a;
complex_traits::variant complex_traits::b{fit::in_place_index<1>, complex_traits::b_value};
complex_traits::variant complex_traits::c{fit::in_place_index<2>, complex_traits::c_value};

const complex_traits::variant complex_traits::const_a;
const complex_traits::variant complex_traits::const_b{fit::in_place_index<1>,
                                                      complex_traits::b_value};
const complex_traits::variant complex_traits::const_c{fit::in_place_index<2>,
                                                      complex_traits::c_value};

template <typename T>
bool accessors() {
  BEGIN_TEST;

  EXPECT_EQ(0, T::a.index());
  EXPECT_TRUE(T::a_value == T::a.template get<0>());
  EXPECT_TRUE(T::a_value == T::const_a.template get<0>());

  EXPECT_EQ(1, T::b.index());
  EXPECT_TRUE(T::b_value == T::b.template get<1>());
  EXPECT_TRUE(T::b_value == T::const_b.template get<1>());

  EXPECT_EQ(2, T::c.index());
  EXPECT_TRUE(T::c_value == T::c.template get<2>());
  EXPECT_TRUE(T::c_value == T::const_c.template get<2>());

  END_TEST;
}

template <typename T>
bool copy_move_assign() {
  BEGIN_TEST;

  using b_type = decltype(T::b_value);
  using c_type = decltype(T::c_value);

  typename T::variant x;
  EXPECT_EQ(0, x.index());
  EXPECT_TRUE(T::a_value == x.template get<0>());

  x = T::b;
  EXPECT_EQ(1, x.index());
  EXPECT_TRUE(fit::holds_alternative<b_type>(x));
  EXPECT_FALSE(fit::holds_alternative<c_type>(x));
  EXPECT_TRUE(T::b_value == x.template get<1>());

  x.template emplace<2>(T::c_value);
  EXPECT_EQ(2, x.index());
  EXPECT_FALSE(fit::holds_alternative<b_type>(x));
  EXPECT_TRUE(fit::holds_alternative<c_type>(x));
  EXPECT_TRUE(T::c_value == x.template get<2>());

  typename T::variant y(T::b);
  EXPECT_EQ(1, y.index());
  EXPECT_TRUE(T::b_value == y.template get<1>());

  x = std::move(y);
  EXPECT_EQ(1, x.index());
  EXPECT_TRUE(T::b_value == x.template get<1>());

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

  x = x;
  EXPECT_EQ(1, x.index());
  EXPECT_TRUE(T::b_value == x.template get<1>());

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  x = std::move(x);
  EXPECT_EQ(1, x.index());
  EXPECT_TRUE(fit::holds_alternative<b_type>(x));
  EXPECT_FALSE(fit::holds_alternative<c_type>(x));
  EXPECT_TRUE(T::b_value == x.template get<1>());

  x = T::a;
  EXPECT_EQ(0, x.index());
  EXPECT_TRUE(T::a_value == x.template get<0>());

  x = T::c;
  typename T::variant z(std::move(x));
  EXPECT_EQ(2, z.index());
  EXPECT_FALSE(fit::holds_alternative<b_type>(z));
  EXPECT_TRUE(fit::holds_alternative<c_type>(z));
  EXPECT_TRUE(T::c_value == z.template get<2>());

  END_TEST;
}

template <typename T>
bool swapping() {
  BEGIN_TEST;

  typename T::variant x;
  EXPECT_EQ(0, x.index());
  EXPECT_TRUE(T::a_value == x.template get<0>());

  typename T::variant y(T::c);
  y.swap(y);
  EXPECT_EQ(2, y.index());
  EXPECT_TRUE(T::c_value == y.template get<2>());

  x.swap(y);
  EXPECT_EQ(2, x.index());
  EXPECT_TRUE(T::c_value == x.template get<2>());
  EXPECT_EQ(0, y.index());
  EXPECT_TRUE(T::a_value == y.template get<0>());

  y.template emplace<2>(T::c2_value);
  x.swap(y);
  EXPECT_EQ(2, x.index());
  EXPECT_TRUE(T::c2_value == x.template get<2>());
  EXPECT_EQ(2, y.index());
  EXPECT_TRUE(T::c_value == y.template get<2>());

  x = T::b;
  y.swap(x);
  EXPECT_EQ(2, x.index());
  EXPECT_TRUE(T::c_value == x.template get<2>());
  EXPECT_EQ(1, y.index());
  EXPECT_TRUE(T::b_value == y.template get<1>());

  x = T::a;
  y.swap(x);
  EXPECT_EQ(1, x.index());
  EXPECT_TRUE(T::b_value == x.template get<1>());
  EXPECT_EQ(0, y.index());
  EXPECT_TRUE(T::a_value == y.template get<0>());

  END_TEST;
}

// Test constexpr behavior.
namespace constexpr_test {
static_assert(literal_traits::variant().index() == 0, "");
static_assert(literal_traits::const_a.index() == 0, "");
static_assert(literal_traits::const_a.get<0>() == literal_traits::a_value, "");
static_assert(literal_traits::const_b.index() == 1, "");
static_assert(literal_traits::const_b.get<1>() == literal_traits::b_value, "");
static_assert(literal_traits::const_c.index() == 2, "");
static_assert(literal_traits::const_c.get<2>() == literal_traits::c_value, "");
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

// These definitions make fit::monostate always compare less than
// |less| and |greater|.
constexpr bool operator==(fit::monostate, greater) { return false; }
constexpr bool operator<=(fit::monostate, greater) { return true; }
constexpr bool operator>=(fit::monostate, greater) { return false; }
constexpr bool operator!=(fit::monostate, greater) { return true; }
constexpr bool operator<(fit::monostate, greater) { return true; }
constexpr bool operator>(fit::monostate, greater) { return false; }

constexpr bool operator==(greater, fit::monostate) { return false; }
constexpr bool operator<=(greater, fit::monostate) { return false; }
constexpr bool operator>=(greater, fit::monostate) { return true; }
constexpr bool operator!=(greater, fit::monostate) { return true; }
constexpr bool operator<(greater, fit::monostate) { return false; }
constexpr bool operator>(greater, fit::monostate) { return true; }

constexpr bool operator==(fit::monostate, less) { return false; }
constexpr bool operator<=(fit::monostate, less) { return true; }
constexpr bool operator>=(fit::monostate, less) { return false; }
constexpr bool operator!=(fit::monostate, less) { return true; }
constexpr bool operator<(fit::monostate, less) { return true; }
constexpr bool operator>(fit::monostate, less) { return false; }

constexpr bool operator==(less, fit::monostate) { return false; }
constexpr bool operator<=(less, fit::monostate) { return false; }
constexpr bool operator>=(less, fit::monostate) { return true; }
constexpr bool operator!=(less, fit::monostate) { return true; }
constexpr bool operator<(less, fit::monostate) { return false; }
constexpr bool operator>(less, fit::monostate) { return true; }

template <typename T, typename U>
constexpr bool match_comparisons(T lhs, U rhs) {
  // Use the following variant for all of the tests below. Note that the types
  // are ordered such that unlike variant comparisons yield a total order.
  // That is: fit::monostate < less < greater.
  using variant = fit::variant<fit::monostate, less, greater>;

  static_assert((variant{lhs} == variant{rhs}) == (lhs == rhs), "");
  static_assert((variant{lhs} != variant{rhs}) == (lhs != rhs), "");
  static_assert((variant{lhs} <= variant{rhs}) == (lhs <= rhs), "");
  static_assert((variant{lhs} >= variant{rhs}) == (lhs >= rhs), "");
  static_assert((variant{lhs} < variant{rhs}) == (lhs < rhs), "");
  static_assert((variant{lhs} > variant{rhs}) == (lhs > rhs), "");

  return true;
}

static_assert(match_comparisons(fit::monostate{}, fit::monostate{}), "");
static_assert(match_comparisons(fit::monostate{}, less{}), "");
static_assert(match_comparisons(fit::monostate{}, greater{}), "");
static_assert(match_comparisons(less{}, fit::monostate{}), "");
static_assert(match_comparisons(less{}, less{}), "");
static_assert(match_comparisons(less{}, greater{}), "");
static_assert(match_comparisons(greater{}, fit::monostate{}), "");
static_assert(match_comparisons(greater{}, less{}), "");
static_assert(match_comparisons(greater{}, greater{}), "");

}  // namespace comparison_tests

// Ensure the variant is copy-constructible only when the types are copyable.
namespace copy_construction_test {
static_assert(std::is_copy_constructible<fit::variant<fit::monostate>>::value, "");
static_assert(!std::is_copy_constructible<fit::variant<fit::monostate, no_copy>>::value, "");
static_assert(std::is_copy_constructible<fit::variant<fit::monostate, no_move>>::value, "");
static_assert(!std::is_copy_constructible<fit::variant<fit::monostate, no_copy_no_move>>::value,
              "");
static_assert(std::is_copy_constructible<literal_traits::variant>::value, "");
static_assert(std::is_copy_constructible<complex_traits::variant>::value, "");
}  // namespace copy_construction_test

// Ensure the variant is copy-assignable only when the types are copyable.
namespace copy_assignment_test {
static_assert(std::is_copy_assignable<fit::variant<fit::monostate>>::value, "");
static_assert(!std::is_copy_assignable<fit::variant<fit::monostate, no_copy>>::value, "");
static_assert(std::is_copy_assignable<fit::variant<fit::monostate, no_move>>::value, "");
static_assert(!std::is_copy_assignable<fit::variant<fit::monostate, no_copy_no_move>>::value, "");
static_assert(std::is_copy_assignable<literal_traits::variant>::value, "");
static_assert(std::is_copy_assignable<complex_traits::variant>::value, "");
}  // namespace copy_assignment_test

// Ensure the variant is move-constructible only when the types are movable.
// Note that copy-constructible types are also considered movable.
namespace move_construction_test {
static_assert(std::is_move_constructible<fit::variant<fit::monostate>>::value, "");
static_assert(std::is_move_constructible<fit::variant<fit::monostate, no_copy>>::value, "");
static_assert(std::is_move_constructible<fit::variant<fit::monostate, no_move>>::value, "");
static_assert(!std::is_move_constructible<fit::variant<fit::monostate, no_copy_no_move>>::value,
              "");
static_assert(std::is_move_constructible<literal_traits::variant>::value, "");
static_assert(std::is_move_constructible<complex_traits::variant>::value, "");
}  // namespace move_construction_test

// Ensure the variant is move-assignable only when the types are movable.
// Note that copy-assignable types are also considered movable.
namespace move_assignment_test {
static_assert(std::is_move_assignable<fit::variant<fit::monostate>>::value, "");
static_assert(std::is_move_assignable<fit::variant<fit::monostate, no_copy>>::value, "");
static_assert(std::is_move_assignable<fit::variant<fit::monostate, no_move>>::value, "");
static_assert(!std::is_move_assignable<fit::variant<fit::monostate, no_copy_no_move>>::value, "");
static_assert(std::is_move_assignable<literal_traits::variant>::value, "");
static_assert(std::is_move_assignable<complex_traits::variant>::value, "");
}  // namespace move_assignment_test

// Ensure that the correct sequence of base types are considered in the
// implementation of variant to ensure that the right methods participate
// in overload resolution.
namespace impl_test {

// Type with a trivial destructor, move, and copy.
namespace trivial_type {
static_assert(std::is_trivially_destructible<fit::variant<fit::monostate, int>>::value, "");
static_assert(std::is_trivially_move_constructible<fit::variant<fit::monostate, int>>::value, "");
static_assert(std::is_trivially_copy_constructible<fit::variant<fit::monostate, int>>::value, "");
static_assert(std::is_trivially_move_assignable<fit::variant<fit::monostate, int>>::value, "");
static_assert(std::is_trivially_copy_assignable<fit::variant<fit::monostate, int>>::value, "");
}  // namespace trivial_type

// Type with a non-trivial destructor implies it has non-trivial move and copy too.
namespace non_trivial_destructor_type {
static_assert(
    !std::is_trivially_destructible<fit::variant<fit::monostate, non_trivial_destructor>>::value,
    "");
static_assert(!std::is_trivially_move_constructible<
                  fit::variant<fit::monostate, non_trivial_destructor>>::value,
              "");
static_assert(!std::is_trivially_copy_constructible<
                  fit::variant<fit::monostate, non_trivial_destructor>>::value,
              "");
static_assert(
    !std::is_trivially_move_assignable<fit::variant<fit::monostate, non_trivial_destructor>>::value,
    "");
static_assert(
    !std::is_trivially_copy_assignable<fit::variant<fit::monostate, non_trivial_destructor>>::value,
    "");
}  // namespace non_trivial_destructor_type

// Type with a non-trivial move constructor actually ends up being trivially
// movable anyhow if it has a trivial copy constructor and destructor.
namespace non_trivial_move_type {
static_assert(std::is_trivially_destructible<fit::variant<fit::monostate, non_trivial_move>>::value,
              "");
static_assert(
    !std::is_trivially_move_constructible<fit::variant<fit::monostate, non_trivial_move>>::value,
    "");
static_assert(
    std::is_trivially_copy_constructible<fit::variant<fit::monostate, non_trivial_move>>::value,
    "");
static_assert(
    !std::is_trivially_move_assignable<fit::variant<fit::monostate, non_trivial_move>>::value, "");
static_assert(
    std::is_trivially_copy_assignable<fit::variant<fit::monostate, non_trivial_move>>::value, "");
}  // namespace non_trivial_move_type

// Type with a non-trivial copy constructor may be trivially movable while not
// trivially copyable.
namespace non_trivial_copy_type {
static_assert(std::is_trivially_destructible<fit::variant<fit::monostate, non_trivial_copy>>::value,
              "");
static_assert(
    std::is_trivially_move_constructible<fit::variant<fit::monostate, non_trivial_copy>>::value,
    "");
static_assert(
    !std::is_trivially_copy_constructible<fit::variant<fit::monostate, non_trivial_copy>>::value,
    "");
static_assert(
    std::is_trivially_move_assignable<fit::variant<fit::monostate, non_trivial_copy>>::value, "");
static_assert(
    !std::is_trivially_copy_assignable<fit::variant<fit::monostate, non_trivial_copy>>::value, "");
}  // namespace non_trivial_copy_type

// std::string is not trivally destructible, movable, or copyable.
namespace string_type {
static_assert(!std::is_trivially_destructible<fit::variant<fit::monostate, std::string>>::value,
              "");
static_assert(
    !std::is_trivially_move_constructible<fit::variant<fit::monostate, std::string>>::value, "");
static_assert(
    !std::is_trivially_copy_constructible<fit::variant<fit::monostate, std::string>>::value, "");
static_assert(!std::is_trivially_move_assignable<fit::variant<fit::monostate, std::string>>::value,
              "");
static_assert(!std::is_trivially_copy_assignable<fit::variant<fit::monostate, std::string>>::value,
              "");
}  // namespace string_type
}  // namespace impl_test

}  // namespace

BEGIN_TEST_CASE(variant_tests)
RUN_TEST(accessors<literal_traits>)
RUN_TEST(accessors<complex_traits>)
RUN_TEST(copy_move_assign<literal_traits>)
RUN_TEST(copy_move_assign<complex_traits>)
RUN_TEST(swapping<literal_traits>)
RUN_TEST(swapping<complex_traits>)
END_TEST_CASE(variant_tests)
