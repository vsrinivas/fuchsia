// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/poll.h>

#include <zxtest/zxtest.h>

namespace {

struct nothing {};

// Basic properties. (Adapted from result_tests.cc)
static_assert(!cpp17::is_constructible_v<fasync::poll<int>>, "");
static_assert(cpp17::is_constructible_v<fasync::poll<int>, fasync::pending>, "");
static_assert(!cpp17::is_constructible_v<fasync::poll<int>, fasync::ready<>>, "");
static_assert(!cpp17::is_constructible_v<fasync::poll<int>, nothing>, "");
static_assert(!cpp17::is_constructible_v<fasync::poll<int>, fasync::ready<nothing>>, "");
static_assert(cpp17::is_constructible_v<fasync::poll<int>, fasync::ready<int>>, "");
static_assert(!cpp17::is_constructible_v<fasync::poll<int>, fasync::ready<fit::success<int>>>, "");

static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>>, "");
static_assert(cpp17::is_constructible_v<fasync::try_poll<int, int>, fasync::pending>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, fasync::ready<>>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, int>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, fasync::ready<int>>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, fit::success<int>>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, fit::error<int>>, "");
static_assert(
    cpp17::is_constructible_v<fasync::try_poll<int, int>, fasync::ready<fit::success<int>>>, "");
static_assert(cpp17::is_constructible_v<fasync::try_poll<int, int>, fasync::ready<fit::error<int>>>,
              "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, nothing>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<int, int>, fasync::ready<nothing>>, "");

static_assert(!cpp17::is_constructible_v<fasync::poll<>>, "");
static_assert(cpp17::is_constructible_v<fasync::poll<>, fasync::pending>, "");
static_assert(cpp17::is_constructible_v<fasync::poll<>, fasync::ready<>>, "");
static_assert(!cpp17::is_constructible_v<fasync::poll<>, nothing>, "");
static_assert(!cpp17::is_constructible_v<fasync::poll<>, fasync::ready<nothing>>, "");

static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>>, "");
static_assert(cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fasync::pending>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fit::failed>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fit::success<>>, "");
static_assert(
    !cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fasync::ready<fit::success<>>>,
    "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, int>, "");
static_assert(
    cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fasync::ready<fit::success<int>>>,
    "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, nothing>, "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fit::success<nothing>>,
              "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>,
                                         fasync::ready<fit::success<nothing>>>,
              "");
static_assert(
    !cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fasync::ready<fit::error<int>>>,
    "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fit::error<nothing>>,
              "");
static_assert(!cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>,
                                         fasync::ready<fit::error<nothing>>>,
              "");
static_assert(
    !cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>, fit::error<fit::failed>>, "");
static_assert(cpp17::is_constructible_v<fasync::try_poll<fit::failed, int>,
                                        fasync::ready<fit::error<fit::failed>>>,
              "");

#if 0 || TEST_DOES_NOT_COMPILE
static_assert(fasync::poll<fasync::pending>{}, "");
static_assert(fasync::poll<fasync::ready<>>{}, "");
static_assert(fasync::try_poll<int, fasync::pending>{}, "");
static_assert(fasync::try_poll<int, fasync::ready<>>{}, "");
#endif

TEST(PollTests, Assignment) {
  [[maybe_unused]] constexpr fasync::poll<> p = fasync::pending();
  [[maybe_unused]] constexpr fasync::poll<> q = fasync::ready();
  [[maybe_unused]] constexpr fasync::poll<int> r = fasync::pending();
  [[maybe_unused]] constexpr fasync::poll<int> s = fasync::done(0);
  [[maybe_unused]] constexpr fasync::try_poll<int, int> t = fasync::pending();
  [[maybe_unused]] constexpr fasync::try_poll<int, int> u = fasync::done(fit::ok(1));
  [[maybe_unused]] constexpr fasync::try_poll<int, int> v = fasync::done(fit::as_error(2));
  [[maybe_unused]] constexpr fasync::try_poll<long, long> w =
      fasync::done(fit::result<int, int>(fit::as_error(2)));
  [[maybe_unused]] constexpr fasync::try_poll<long, long> x =
      fasync::poll(fasync::done(fit::result<int, int>(fit::as_error(2))));
  [[maybe_unused]] constexpr fasync::try_poll<long, long> y = fasync::done(fit::as_error(2));
  [[maybe_unused]] constexpr fasync::poll<int> pp = fasync::done(1);
  [[maybe_unused]] constexpr fasync::try_poll<int, int> tp = fasync::done(fit::ok(1));
  [[maybe_unused]] constexpr fasync::poll<> ppp = fasync::done();
  struct my_struct {
    constexpr my_struct(int aa, int bb) : a(aa), b(bb) {}
    int a;
    int b;
  };
  [[maybe_unused]] constexpr fasync::poll<my_struct> structp = fasync::ready<my_struct>(1, 2);
  static_assert(structp.output().a == 1, "");
  static_assert(structp.output().b == 2, "");
}

#if defined(__Fuchsia__)

TEST(PollTests, Abort) {
  {
    fasync::poll<> poll{fasync::pending()};
    EXPECT_TRUE(poll.is_pending());
    EXPECT_FALSE(poll.is_ready());
    // check_output(poll);
  }
  {
    const fasync::poll<> poll{fasync::pending()};
    EXPECT_TRUE(poll.is_pending());
    EXPECT_FALSE(poll.is_ready());
    // check_output(poll);
  }
  {
    fasync::poll<> poll{fasync::ready()};
    EXPECT_FALSE(poll.is_pending());
    EXPECT_TRUE(poll.is_ready());
    // check_output(poll);
  }
  {
    const fasync::poll<> poll{fasync::ready()};
    EXPECT_FALSE(poll.is_pending());
    EXPECT_TRUE(poll.is_ready());
    // check_output(poll);
  }

  // Validate that accessing the output of a pending poll aborts.
  ASSERT_DEATH(([] {
    fasync::poll<nothing> poll{fasync::pending()};
    EXPECT_TRUE(poll.is_pending());
    EXPECT_FALSE(poll.is_ready());
    poll.output();
  }));
  ASSERT_DEATH(([] {
    const fasync::poll<nothing> poll{fasync::pending()};
    EXPECT_TRUE(poll.is_pending());
    EXPECT_FALSE(poll.is_ready());
    poll.output();
  }));
}

#endif  // defined(__Fuchsia__)

namespace comparison_tests {

struct greater {};
struct less {};
struct empty {};

constexpr bool operator==(greater, greater) { return true; }
constexpr bool operator<=(greater, greater) { return true; }
constexpr bool operator>=(greater, greater) { return true; }
constexpr bool operator!=(greater, greater) { return false; }
constexpr bool operator<(greater, greater) { return false; }
constexpr bool operator>(greater, greater) { return false; }
// constexpr std::strong_ordering operator<=>(greater, greater) { return
// std::strong_ordering::equal; }

constexpr bool operator==(less, less) { return true; }
constexpr bool operator<=(less, less) { return true; }
constexpr bool operator>=(less, less) { return true; }
constexpr bool operator!=(less, less) { return false; }
constexpr bool operator<(less, less) { return false; }
constexpr bool operator>(less, less) { return false; }
// constexpr std::strong_ordering operator<=>(less, less) { return std::strong_ordering::equal; }

constexpr bool operator==(greater, less) { return false; }
constexpr bool operator<=(greater, less) { return false; }
constexpr bool operator>=(greater, less) { return true; }
constexpr bool operator!=(greater, less) { return true; }
constexpr bool operator<(greater, less) { return false; }
constexpr bool operator>(greater, less) { return true; }
// constexpr std::strong_ordering operator<=>(greater, less) { return std::strong_ordering::greater;
// }

constexpr bool operator==(less, greater) { return false; }
constexpr bool operator<=(less, greater) { return true; }
constexpr bool operator>=(less, greater) { return false; }
constexpr bool operator!=(less, greater) { return true; }
constexpr bool operator<(less, greater) { return true; }
constexpr bool operator>(less, greater) { return false; }
// constexpr std::strong_ordering operator<=>(less, greater) { return std::strong_ordering::less; }

// Note these definitions match the empty-to-other, other-to-empty, and
// empty-to-empty comparison behavior of fit::result for convenience in
// exhaustive testing.
constexpr bool operator==(empty, greater) { return false; }
constexpr bool operator<=(empty, greater) { return true; }
constexpr bool operator>=(empty, greater) { return false; }
constexpr bool operator!=(empty, greater) { return true; }
constexpr bool operator<(empty, greater) { return true; }
constexpr bool operator>(empty, greater) { return false; }
// constexpr std::strong_ordering operator<=>(empty, greater) { return std::strong_ordering::less; }

constexpr bool operator==(greater, empty) { return false; }
constexpr bool operator<=(greater, empty) { return false; }
constexpr bool operator>=(greater, empty) { return true; }
constexpr bool operator!=(greater, empty) { return true; }
constexpr bool operator<(greater, empty) { return false; }
constexpr bool operator>(greater, empty) { return true; }
// constexpr std::strong_ordering operator<=>(greater, empty) { return
// std::strong_ordering::greater; }

constexpr bool operator==(empty, less) { return false; }
constexpr bool operator<=(empty, less) { return true; }
constexpr bool operator>=(empty, less) { return false; }
constexpr bool operator!=(empty, less) { return true; }
constexpr bool operator<(empty, less) { return true; }
constexpr bool operator>(empty, less) { return false; }
// constexpr std::strong_ordering operator<=>(empty, less) { return std::strong_ordering::less; }

constexpr bool operator==(less, empty) { return false; }
constexpr bool operator<=(less, empty) { return false; }
constexpr bool operator>=(less, empty) { return true; }
constexpr bool operator!=(less, empty) { return true; }
constexpr bool operator<(less, empty) { return false; }
constexpr bool operator>(less, empty) { return true; }
// constexpr std::strong_ordering operator<=>(less, empty) { return std::strong_ordering::greater; }

constexpr bool operator==(empty, empty) { return true; }
constexpr bool operator<=(empty, empty) { return true; }
constexpr bool operator>=(empty, empty) { return true; }
constexpr bool operator!=(empty, empty) { return false; }
constexpr bool operator<(empty, empty) { return false; }
constexpr bool operator>(empty, empty) { return false; }
// constexpr std::strong_ordering operator<=>(empty, empty) { return std::strong_ordering::equal; }

template <typename T, typename U>
constexpr bool match_comparisons(T, U) {
  constexpr T lhs{};
  constexpr U rhs{};

  constexpr fasync::poll<T> ready_lhs{fasync::done(lhs)};
  constexpr fasync::poll<U> ready_rhs{fasync::done(rhs)};
  constexpr fasync::poll<T> pending_lhs{fasync::pending()};
  constexpr fasync::poll<U> pending_rhs{fasync::pending()};

  // Both result operands.
  static_assert((ready_lhs == ready_rhs) == (lhs == rhs), "");
  static_assert((ready_lhs != ready_rhs) == (lhs != rhs), "");
  static_assert((ready_lhs <= ready_rhs) == (lhs <= rhs), "");
  static_assert((ready_lhs >= ready_rhs) == (lhs >= rhs), "");
  static_assert((ready_lhs < ready_rhs) == (lhs < rhs), "");
  static_assert((ready_lhs > ready_rhs) == (lhs > rhs), "");

  static_assert((pending_lhs == ready_rhs) == (empty{} == rhs), "");
  static_assert((pending_lhs != ready_rhs) == (empty{} != rhs), "");
  static_assert((pending_lhs <= ready_rhs) == (empty{} <= rhs), "");
  static_assert((pending_lhs >= ready_rhs) == (empty{} >= rhs), "");
  static_assert((pending_lhs < ready_rhs) == (empty{} < rhs), "");
  static_assert((pending_lhs > ready_rhs) == (empty{} > rhs), "");

  static_assert((ready_lhs == pending_rhs) == (lhs == empty{}), "");
  static_assert((ready_lhs != pending_rhs) == (lhs != empty{}), "");
  static_assert((ready_lhs <= pending_rhs) == (lhs <= empty{}), "");
  static_assert((ready_lhs >= pending_rhs) == (lhs >= empty{}), "");
  static_assert((ready_lhs < pending_rhs) == (lhs < empty{}), "");
  static_assert((ready_lhs > pending_rhs) == (lhs > empty{}), "");

  static_assert((pending_lhs == pending_rhs) == (empty{} == empty{}), "");
  static_assert((pending_lhs != pending_rhs) == (empty{} != empty{}), "");
  static_assert((pending_lhs <= pending_rhs) == (empty{} <= empty{}), "");
  static_assert((pending_lhs >= pending_rhs) == (empty{} >= empty{}), "");
  static_assert((pending_lhs < pending_rhs) == (empty{} < empty{}), "");
  static_assert((pending_lhs > pending_rhs) == (empty{} > empty{}), "");

  // Right hand result only.
  static_assert((lhs == ready_rhs) == (lhs == rhs), "");
  static_assert((lhs != ready_rhs) == (lhs != rhs), "");
  static_assert((lhs <= ready_rhs) == (lhs <= rhs), "");
  static_assert((lhs >= ready_rhs) == (lhs >= rhs), "");
  static_assert((lhs < ready_rhs) == (lhs < rhs), "");
  static_assert((lhs > ready_rhs) == (lhs > rhs), "");

  static_assert((lhs == pending_rhs) == (lhs == empty{}), "");
  static_assert((lhs != pending_rhs) == (lhs != empty{}), "");
  static_assert((lhs <= pending_rhs) == (lhs <= empty{}), "");
  static_assert((lhs >= pending_rhs) == (lhs >= empty{}), "");
  static_assert((lhs < pending_rhs) == (lhs < empty{}), "");
  static_assert((lhs > pending_rhs) == (lhs > empty{}), "");

  // Left hand result only.
  static_assert((ready_lhs == rhs) == (lhs == rhs), "");
  static_assert((ready_lhs != rhs) == (lhs != rhs), "");
  static_assert((ready_lhs <= rhs) == (lhs <= rhs), "");
  static_assert((ready_lhs >= rhs) == (lhs >= rhs), "");
  static_assert((ready_lhs < rhs) == (lhs < rhs), "");
  static_assert((ready_lhs > rhs) == (lhs > rhs), "");

  static_assert((pending_lhs == rhs) == (empty{} == rhs), "");
  static_assert((pending_lhs != rhs) == (empty{} != rhs), "");
  static_assert((pending_lhs <= rhs) == (empty{} <= rhs), "");
  static_assert((pending_lhs >= rhs) == (empty{} >= rhs), "");
  static_assert((pending_lhs < rhs) == (empty{} < rhs), "");
  static_assert((pending_lhs > rhs) == (empty{} > rhs), "");

  return true;
}

static_assert(match_comparisons(greater{}, greater{}), "");
static_assert(match_comparisons(greater{}, less{}), "");
static_assert(match_comparisons(less{}, greater{}), "");
static_assert(match_comparisons(less{}, less{}), "");

static_assert(fasync::poll(fasync::ready()) == fasync::ready());
static_assert(fasync::poll(fasync::ready()) == fasync::poll(fasync::ready()));
static_assert(fasync::poll(fasync::ready(1)) == fasync::ready());
static_assert(fasync::poll(fasync::ready(1)) == fasync::poll(fasync::ready()));

}  // namespace comparison_tests
}  // namespace
