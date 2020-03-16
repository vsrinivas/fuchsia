// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/unittest/unittest.h>

// TODO(mcgrathr): unittests are ignored, leaving unused definitions here.
// Eventually the test code should only be compiled at all in configurations
// that actually use it.

#if LK_DEBUGLEVEL != 0

#include <ktl/move.h>
#include <ktl/type_traits.h>
#include <ktl/variant.h>

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
  using variant = ktl::variant<ktl::monostate, int, long int>;

  static constexpr ktl::monostate a_value{};
  static constexpr int b_value = 10;
  static constexpr long int c_value = 25;
  static constexpr long int c2_value = 42;

  static variant a, b, c;
  static constexpr variant const_a{};
  static constexpr variant const_b{ktl::in_place_index<1>, b_value};
  static constexpr variant const_c{ktl::in_place_index<2>, c_value};
};

literal_traits::variant literal_traits::a;
literal_traits::variant literal_traits::b{ktl::in_place_index<1>, literal_traits::b_value};
literal_traits::variant literal_traits::c{ktl::in_place_index<2>, literal_traits::c_value};

struct complex_traits {
  using variant = ktl::variant<ktl::monostate, int, const char*>;

  static const ktl::monostate a_value;
  static const int b_value;
  static const char* const c_value;
  static const char* const c2_value;

  static variant a, b, c;
  static const variant const_a;
  static const variant const_b;
  static const variant const_c;
};

const ktl::monostate complex_traits::a_value{};
const int complex_traits::b_value = 10;
const char* const complex_traits::c_value = "test";
const char* const complex_traits::c2_value = "another";

complex_traits::variant complex_traits::a;
complex_traits::variant complex_traits::b{ktl::in_place_index<1>, complex_traits::b_value};
complex_traits::variant complex_traits::c{ktl::in_place_index<2>, complex_traits::c_value};

const complex_traits::variant complex_traits::const_a;
const complex_traits::variant complex_traits::const_b{ktl::in_place_index<1>,
                                                      complex_traits::b_value};
const complex_traits::variant complex_traits::const_c{ktl::in_place_index<2>,
                                                      complex_traits::c_value};

template <typename T>
bool accessors() {
  BEGIN_TEST;

  EXPECT_EQ(size_t{0}, T::a.index());
  EXPECT_TRUE(T::a_value == ktl::get<0>(T::a));
  EXPECT_TRUE(T::a_value == ktl::get<0>(T::const_a));

  EXPECT_EQ(size_t{1}, T::b.index());
  EXPECT_TRUE(T::b_value == ktl::get<1>(T::b));
  EXPECT_TRUE(T::b_value == ktl::get<1>(T::const_b));

  EXPECT_EQ(size_t{2}, T::c.index());
  EXPECT_TRUE(T::c_value == ktl::get<2>(T::c));
  EXPECT_TRUE(T::c_value == ktl::get<2>(T::const_c));

  END_TEST;
}

template <typename T>
bool copy_move_assign() {
  BEGIN_TEST;

  using b_type = ktl::decay_t<decltype(T::b_value)>;
  using c_type = ktl::decay_t<decltype(T::c_value)>;

  typename T::variant x;
  EXPECT_EQ(size_t{0}, x.index());
  EXPECT_TRUE(T::a_value == ktl::get<0>(x));

  x = T::b;
  EXPECT_EQ(size_t{1}, x.index());
  EXPECT_TRUE(ktl::holds_alternative<b_type>(x));
  EXPECT_FALSE(ktl::holds_alternative<c_type>(x));
  EXPECT_TRUE(T::b_value == ktl::get<1>(x));

  x.template emplace<2>(T::c_value);
  EXPECT_EQ(size_t{2}, x.index());
  EXPECT_FALSE(ktl::holds_alternative<b_type>(x));
  EXPECT_TRUE(ktl::holds_alternative<c_type>(x));
  EXPECT_TRUE(T::c_value == ktl::get<2>(x));

  typename T::variant y(T::b);
  EXPECT_EQ(size_t{1}, y.index());
  EXPECT_TRUE(T::b_value == ktl::get<1>(y));

  x = ktl::move(y);
  EXPECT_EQ(size_t{1}, x.index());
  EXPECT_TRUE(T::b_value == ktl::get<1>(x));

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

  x = x;
  EXPECT_EQ(size_t{1}, x.index());
  EXPECT_TRUE(T::b_value == ktl::get<1>(x));

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  x = ktl::move(x);
  EXPECT_EQ(size_t{1}, x.index());
  EXPECT_TRUE(ktl::holds_alternative<b_type>(x));
  EXPECT_FALSE(ktl::holds_alternative<c_type>(x));
  EXPECT_TRUE(T::b_value == ktl::get<1>(x));

  x = T::a;
  EXPECT_EQ(size_t{0}, x.index());
  EXPECT_TRUE(T::a_value == ktl::get<0>(x));

  x = T::c;
  typename T::variant z(ktl::move(x));
  EXPECT_EQ(size_t{2}, z.index());
  EXPECT_FALSE(ktl::holds_alternative<b_type>(z));
  EXPECT_TRUE(ktl::holds_alternative<c_type>(z));
  EXPECT_TRUE(T::c_value == ktl::get<2>(z));

  END_TEST;
}

template <typename T>
bool swapping() {
  BEGIN_TEST;

  typename T::variant x;
  EXPECT_EQ(size_t{0}, x.index());
  EXPECT_TRUE(T::a_value == ktl::get<0>(x));

  typename T::variant y(T::c);
  y.swap(y);
  EXPECT_EQ(size_t{2}, y.index());
  EXPECT_TRUE(T::c_value == ktl::get<2>(y));

  x.swap(y);
  EXPECT_EQ(size_t{2}, x.index());
  EXPECT_TRUE(T::c_value == ktl::get<2>(x));
  EXPECT_EQ(size_t{0}, y.index());
  EXPECT_TRUE(T::a_value == ktl::get<0>(y));

  y.template emplace<2>(T::c2_value);
  x.swap(y);
  EXPECT_EQ(size_t{2}, x.index());
  EXPECT_TRUE(T::c2_value == ktl::get<2>(x));
  EXPECT_EQ(size_t{2}, y.index());
  EXPECT_TRUE(T::c_value == ktl::get<2>(y));

  x = T::b;
  y.swap(x);
  EXPECT_EQ(size_t{2}, x.index());
  EXPECT_TRUE(T::c_value == ktl::get<2>(x));
  EXPECT_EQ(size_t{1}, y.index());
  EXPECT_TRUE(T::b_value == ktl::get<1>(y));

  x = T::a;
  y.swap(x);
  EXPECT_EQ(size_t{1}, x.index());
  EXPECT_TRUE(T::b_value == ktl::get<1>(x));
  EXPECT_EQ(size_t{0}, y.index());
  EXPECT_TRUE(T::a_value == ktl::get<0>(y));

  END_TEST;
}

// Test constexpr behavior.
namespace constexpr_test {
static_assert(literal_traits::variant().index() == 0, "");
static_assert(literal_traits::const_a.index() == 0, "");
static_assert(ktl::get<0>(literal_traits::const_a) == literal_traits::a_value, "");
static_assert(literal_traits::const_b.index() == 1, "");
static_assert(ktl::get<1>(literal_traits::const_b) == literal_traits::b_value, "");
static_assert(literal_traits::const_c.index() == 2, "");
static_assert(ktl::get<2>(literal_traits::const_c) == literal_traits::c_value, "");
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

// These definitions make ktl::monostate always compare less than
// |less| and |greater|.
constexpr bool operator==(ktl::monostate, greater) { return false; }
constexpr bool operator<=(ktl::monostate, greater) { return true; }
constexpr bool operator>=(ktl::monostate, greater) { return false; }
constexpr bool operator!=(ktl::monostate, greater) { return true; }
constexpr bool operator<(ktl::monostate, greater) { return true; }
constexpr bool operator>(ktl::monostate, greater) { return false; }

constexpr bool operator==(greater, ktl::monostate) { return false; }
constexpr bool operator<=(greater, ktl::monostate) { return false; }
constexpr bool operator>=(greater, ktl::monostate) { return true; }
constexpr bool operator!=(greater, ktl::monostate) { return true; }
constexpr bool operator<(greater, ktl::monostate) { return false; }
constexpr bool operator>(greater, ktl::monostate) { return true; }

constexpr bool operator==(ktl::monostate, less) { return false; }
constexpr bool operator<=(ktl::monostate, less) { return true; }
constexpr bool operator>=(ktl::monostate, less) { return false; }
constexpr bool operator!=(ktl::monostate, less) { return true; }
constexpr bool operator<(ktl::monostate, less) { return true; }
constexpr bool operator>(ktl::monostate, less) { return false; }

constexpr bool operator==(less, ktl::monostate) { return false; }
constexpr bool operator<=(less, ktl::monostate) { return false; }
constexpr bool operator>=(less, ktl::monostate) { return true; }
constexpr bool operator!=(less, ktl::monostate) { return true; }
constexpr bool operator<(less, ktl::monostate) { return false; }
constexpr bool operator>(less, ktl::monostate) { return true; }

template <typename T, typename U>
constexpr bool match_comparisons(T lhs, U rhs) {
  // Use the following variant for all of the tests below. Note that the types
  // are ordered such that unlike variant comparisons yield a total order.
  // That is: ktl::monostate < less < greater.
  using variant = ktl::variant<ktl::monostate, less, greater>;

  static_assert((variant{lhs} == variant{rhs}) == (lhs == rhs), "");
  static_assert((variant{lhs} != variant{rhs}) == (lhs != rhs), "");
  static_assert((variant{lhs} <= variant{rhs}) == (lhs <= rhs), "");
  static_assert((variant{lhs} >= variant{rhs}) == (lhs >= rhs), "");
  static_assert((variant{lhs} < variant{rhs}) == (lhs < rhs), "");
  static_assert((variant{lhs} > variant{rhs}) == (lhs > rhs), "");

  return true;
}

static_assert(match_comparisons(ktl::monostate{}, ktl::monostate{}), "");
static_assert(match_comparisons(ktl::monostate{}, less{}), "");
static_assert(match_comparisons(ktl::monostate{}, greater{}), "");
static_assert(match_comparisons(less{}, ktl::monostate{}), "");
static_assert(match_comparisons(less{}, less{}), "");
static_assert(match_comparisons(less{}, greater{}), "");
static_assert(match_comparisons(greater{}, ktl::monostate{}), "");
static_assert(match_comparisons(greater{}, less{}), "");
static_assert(match_comparisons(greater{}, greater{}), "");

}  // namespace comparison_tests

// Ensure the variant is copy-constructible only when the types are copyable.
namespace copy_construction_test {
static_assert(ktl::is_copy_constructible<ktl::variant<ktl::monostate>>::value, "");
static_assert(!ktl::is_copy_constructible<ktl::variant<ktl::monostate, no_copy>>::value, "");
static_assert(ktl::is_copy_constructible<ktl::variant<ktl::monostate, no_move>>::value, "");
static_assert(!ktl::is_copy_constructible<ktl::variant<ktl::monostate, no_copy_no_move>>::value,
              "");
static_assert(ktl::is_copy_constructible<literal_traits::variant>::value, "");
static_assert(ktl::is_copy_constructible<complex_traits::variant>::value, "");
}  // namespace copy_construction_test

// Ensure the variant is copy-assignable only when the types are copyable.
namespace copy_assignment_test {
static_assert(ktl::is_copy_assignable<ktl::variant<ktl::monostate>>::value, "");
static_assert(!ktl::is_copy_assignable<ktl::variant<ktl::monostate, no_copy>>::value, "");
static_assert(ktl::is_copy_assignable<ktl::variant<ktl::monostate, no_move>>::value, "");
static_assert(!ktl::is_copy_assignable<ktl::variant<ktl::monostate, no_copy_no_move>>::value, "");
static_assert(ktl::is_copy_assignable<literal_traits::variant>::value, "");
static_assert(ktl::is_copy_assignable<complex_traits::variant>::value, "");
}  // namespace copy_assignment_test

// Ensure the variant is move-constructible only when the types are movable.
// Note that copy-constructible types are also considered movable.
namespace move_construction_test {
static_assert(ktl::is_move_constructible<ktl::variant<ktl::monostate>>::value, "");
static_assert(ktl::is_move_constructible<ktl::variant<ktl::monostate, no_copy>>::value, "");
static_assert(ktl::is_move_constructible<ktl::variant<ktl::monostate, no_move>>::value, "");
static_assert(!ktl::is_move_constructible<ktl::variant<ktl::monostate, no_copy_no_move>>::value,
              "");
static_assert(ktl::is_move_constructible<literal_traits::variant>::value, "");
static_assert(ktl::is_move_constructible<complex_traits::variant>::value, "");
}  // namespace move_construction_test

// Ensure the variant is move-assignable only when the types are movable.
// Note that copy-assignable types are also considered movable.
namespace move_assignment_test {
static_assert(ktl::is_move_assignable<ktl::variant<ktl::monostate>>::value, "");
static_assert(ktl::is_move_assignable<ktl::variant<ktl::monostate, no_copy>>::value, "");
static_assert(ktl::is_move_assignable<ktl::variant<ktl::monostate, no_move>>::value, "");
static_assert(!ktl::is_move_assignable<ktl::variant<ktl::monostate, no_copy_no_move>>::value, "");
static_assert(ktl::is_move_assignable<literal_traits::variant>::value, "");
static_assert(ktl::is_move_assignable<complex_traits::variant>::value, "");
}  // namespace move_assignment_test

// Ensure that the correct sequence of base types are considered in the
// implementation of variant to ensure that the right methods participate
// in overload resolution.
namespace impl_test {

// Type with a trivial destructor, move, and copy.
namespace trivial_type {
static_assert(ktl::is_trivially_destructible<ktl::variant<ktl::monostate, int>>::value, "");
static_assert(ktl::is_trivially_move_constructible<ktl::variant<ktl::monostate, int>>::value, "");
static_assert(ktl::is_trivially_copy_constructible<ktl::variant<ktl::monostate, int>>::value, "");
static_assert(ktl::is_trivially_move_assignable<ktl::variant<ktl::monostate, int>>::value, "");
static_assert(ktl::is_trivially_copy_assignable<ktl::variant<ktl::monostate, int>>::value, "");
}  // namespace trivial_type

// Type with a non-trivial destructor implies it has non-trivial move and copy too.
namespace non_trivial_destructor_type {
static_assert(
    !ktl::is_trivially_destructible<ktl::variant<ktl::monostate, non_trivial_destructor>>::value,
    "");
static_assert(!ktl::is_trivially_move_constructible<
                  ktl::variant<ktl::monostate, non_trivial_destructor>>::value,
              "");
static_assert(!ktl::is_trivially_copy_constructible<
                  ktl::variant<ktl::monostate, non_trivial_destructor>>::value,
              "");
static_assert(
    !ktl::is_trivially_move_assignable<ktl::variant<ktl::monostate, non_trivial_destructor>>::value,
    "");
static_assert(
    !ktl::is_trivially_copy_assignable<ktl::variant<ktl::monostate, non_trivial_destructor>>::value,
    "");
}  // namespace non_trivial_destructor_type

// Type with a non-trivial move constructor actually ends up being trivially
// movable anyhow if it has a trivial copy constructor and destructor.
namespace non_trivial_move_type {
static_assert(ktl::is_trivially_destructible<ktl::variant<ktl::monostate, non_trivial_move>>::value,
              "");
static_assert(
    !ktl::is_trivially_move_constructible<ktl::variant<ktl::monostate, non_trivial_move>>::value,
    "");
static_assert(
    ktl::is_trivially_copy_constructible<ktl::variant<ktl::monostate, non_trivial_move>>::value,
    "");
static_assert(
    !ktl::is_trivially_move_assignable<ktl::variant<ktl::monostate, non_trivial_move>>::value, "");
static_assert(
    ktl::is_trivially_copy_assignable<ktl::variant<ktl::monostate, non_trivial_move>>::value, "");
}  // namespace non_trivial_move_type

// Type with a non-trivial copy constructor may be trivially movable while not
// trivially copyable.
namespace non_trivial_copy_type {
static_assert(ktl::is_trivially_destructible<ktl::variant<ktl::monostate, non_trivial_copy>>::value,
              "");
static_assert(
    ktl::is_trivially_move_constructible<ktl::variant<ktl::monostate, non_trivial_copy>>::value,
    "");
static_assert(
    !ktl::is_trivially_copy_constructible<ktl::variant<ktl::monostate, non_trivial_copy>>::value,
    "");
static_assert(
    ktl::is_trivially_move_assignable<ktl::variant<ktl::monostate, non_trivial_copy>>::value, "");
static_assert(
    !ktl::is_trivially_copy_assignable<ktl::variant<ktl::monostate, non_trivial_copy>>::value, "");
}  // namespace non_trivial_copy_type

}  // namespace impl_test

}  // namespace

UNITTEST_START_TESTCASE(variant_tests)
UNITTEST("ktl::variant accessors, literal", accessors<literal_traits>)
UNITTEST("ktl::variant accessors, compelx", accessors<complex_traits>)
UNITTEST("ktl::variant copy/move/assign, literal", copy_move_assign<literal_traits>)
UNITTEST("ktl::variant copy/move/assign, complex", copy_move_assign<complex_traits>)
UNITTEST("ktl::variant swapping, literal", swapping<literal_traits>)
UNITTEST("ktl::variant swapping, complex", swapping<complex_traits>)
UNITTEST_END_TESTCASE(variant_tests, "variant", "ktl::variant tests")

#endif  // LK_DEBUGLEVEL != 0
