// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/functional.h>
#include <lib/stdcompat/tuple.h>

#include "gtest.h"

namespace {

constexpr auto noop = [] {};
constexpr auto take_all = [](auto&&...) {};
constexpr auto lambda_add_one = [](int i) { return i + 1; };

int func_add_one(int i) { return i + 1; }

struct member_pointers {
  virtual int pmf_add_one(int i) { return i + 1; }
  int (*pmd_add_one)(int) = func_add_one;
};

struct liar : member_pointers {
  int pmf_add_one(int i) override { return i + 2; }
};

TEST(InvokeTest, Basic) {
  static_assert(std::is_same_v<decltype(cpp20::invoke(noop)), void> == true, "");
  static_assert(cpp20::invoke([] { return 123; }) == 123, "");
  static_assert(cpp20::invoke(lambda_add_one, 1) == 2, "");
}

TEST(InvokeTest, ForwardingReferences) {
  int i = 1;
  // Wrap in lambda so we can decltype() (also no explicit template parameter).
  constexpr auto forward = [](auto&& t) -> decltype(auto) { return std::forward<decltype(t)>(t); };
  static_assert(std::is_same_v<decltype(cpp20::invoke(forward, 1)), int&&> == true, "");
  static_assert(std::is_same_v<decltype(cpp20::invoke(forward, i)), int&> == true, "");
  static_assert(std::is_same_v<decltype(cpp20::invoke(forward, std::move(i))), int&&> == true, "");
}

TEST(InvokeTest, PointersToMember) {
  member_pointers mp;
  liar lp;
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmf_add_one, mp, 1), 2);
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmf_add_one, lp, 1), 3);
  // Invoke PMD then invoke the result (it's a function pointer to func_add_one)
  EXPECT_EQ(cpp20::invoke(cpp20::invoke(&member_pointers::pmd_add_one, lp), 1), 2);

  static_assert(std::is_same_v<decltype(cpp20::invoke(take_all)), void> == true, "");
  static_assert(
      std::is_same_v<decltype(cpp20::invoke(take_all, nullptr, mp, std::tuple())), void> == true,
      "");
}

TEST(InvokeTest, GenericCallables) {
  // std::make_tuple itself is a template so we can't use it directly but we can wrap it.
  constexpr auto make_tuple = [](auto&&... ts) {
    return std::make_tuple(std::forward<decltype(ts)>(ts)...);
  };
  EXPECT_EQ(cpp20::invoke(make_tuple, 1, std::string("asdf"), std::tuple<>()),
            std::make_tuple(1, std::string("asdf"), std::tuple<>()));
}

TEST(InvokeTest, SpecialCases) {
  liar lp;
  // Special handling of std::reference_wrapper per [func.require] ¶ 2 and 5
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmf_add_one, std::ref(lp), 2), 4);
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmd_add_one, std::ref(lp))(2), 3);

  // Handling of dereferenceable entities per [func.require] ¶ 4 and 6
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmf_add_one, &lp, 2), 4);
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmd_add_one, &lp)(2), 3);
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmf_add_one, std::optional<liar>(liar()), 2), 4);
  EXPECT_EQ(cpp20::invoke(&member_pointers::pmd_add_one, std::optional<liar>(liar()))(2), 3);
}

template <typename T>
constexpr auto reduce(T&& only) {
  return std::forward<T>(only);
}

template <typename First, typename Second, typename... Args>
constexpr auto reduce(First&& first, Second&& second, Args&&... args) {
  return std::forward<First>(first) +
         reduce(std::forward<Second>(second), std::forward<Args>(args)...);
}

constexpr auto call_reduce = [](auto&&... args) {
  return reduce(std::forward<decltype(args)>(args)...);
};

template <size_t N, typename... Args, std::enable_if_t<(N == 1), bool> = true>
constexpr auto reduce_bound(Args&&... args) {
  return reduce(std::forward<Args>(args)...);
}

template <size_t N, typename... Args, std::enable_if_t<(N > 1), bool> = true>
constexpr auto reduce_bound(Args&&... args) {
  return [call_with_args =
              cpp20::bind_front(call_reduce, std::forward<Args>(args)...)](auto&&... next) {
    return reduce_bound<N - 1>(std::move(call_with_args)(std::forward<decltype(next)>(next)...));
  };
}

TEST(BindFrontTest, Currying) {
  static_assert(reduce(1, 2, 3, 4, 5) == 15, "");

  static_assert(cpp20::bind_front(call_reduce)(1, 2, 3, 4, 5) == 15, "");
  static_assert(cpp20::bind_front(call_reduce, 1)(2, 3, 4, 5) == 15, "");
  static_assert(cpp20::bind_front(call_reduce, 1, 2)(3, 4, 5) == 15, "");
  static_assert(cpp20::bind_front(call_reduce, 1, 2, 3)(4, 5) == 15, "");
  static_assert(cpp20::bind_front(call_reduce, 1, 2, 3, 4)(5) == 15, "");
  static_assert(cpp20::bind_front(call_reduce, 1, 2, 3, 4, 5)() == 15, "");

  static_assert(reduce_bound<1>(1, 2, 3, 4, 5) == 15, "");

  static_assert(reduce_bound<2>(1)(2, 3, 4, 5) == 15, "");
  static_assert(reduce_bound<2>(1, 2)(3, 4, 5) == 15, "");
  static_assert(reduce_bound<2>(1, 2, 3)(4, 5) == 15, "");
  static_assert(reduce_bound<2>(1, 2, 3, 4)(5) == 15, "");

  static_assert(reduce_bound<3>(1)(2)(3, 4, 5) == 15, "");
  static_assert(reduce_bound<3>(1)(2, 3)(4, 5) == 15, "");
  static_assert(reduce_bound<3>(1)(2, 3, 4)(5) == 15, "");
  static_assert(reduce_bound<3>(1, 2)(3)(4, 5) == 15, "");
  static_assert(reduce_bound<3>(1, 2)(3, 4)(5) == 15, "");
  static_assert(reduce_bound<3>(1, 2, 3)(4)(5) == 15, "");

  static_assert(reduce_bound<4>(1)(2)(3)(4, 5) == 15, "");
  static_assert(reduce_bound<4>(1)(2)(3, 4)(5) == 15, "");
  static_assert(reduce_bound<4>(1)(2, 3)(4)(5) == 15, "");
  static_assert(reduce_bound<4>(1, 2)(3)(4)(5) == 15, "");

  static_assert(reduce_bound<5>(1)(2)(3)(4)(5) == 15, "");

  // And these extra ones where we don't even give it a number (they mess up our perfect grid and
  // multiply the number of cases by a lot, so I won't include any more)
  static_assert(reduce_bound<2>()(1, 2, 3, 4, 5) == 15, "");
  static_assert(reduce_bound<2>(1, 2, 3, 4, 5)() == 15, "");
}

TEST(BindFrontTest, BindCopyable) {
  constexpr auto one_plus_one = cpp20::bind_front(lambda_add_one, 1);
  static_assert(one_plus_one() == 2, "");

  constexpr auto one_plus_two = cpp20::bind_front(func_add_one, 2);
  EXPECT_EQ(one_plus_two(), 3);

  const std::string empty;
  auto echo_string = cpp20::bind_front(call_reduce, empty);
  auto test_if_copyable = echo_string;

  EXPECT_EQ(echo_string("asdf"), "asdf");
  EXPECT_EQ(test_if_copyable("jkl"), "jkl");

  constexpr char dot = '.';
  const std::string space = " ";
  const std::string words[][4] = {
      {"The", "quick", "brown", "fox"}, {"jumped", "over"}, {"the", "lazy", "dog"}};
  EXPECT_EQ(reduce_bound<3>(words[0][0], space, words[0][1], space, words[0][2], space, words[0][3],
                            space)(words[1][0], space, words[1][1], space)(
                words[2][0], space, words[2][1], space, words[2][2], dot),
            "The quick brown fox jumped over the lazy dog.");
}

TEST(BindFrontTest, BindMoveOnly) {
  auto ptr = std::make_unique<int>(3);
  constexpr auto deref = [](auto&& ptr) { return *ptr; };
  auto call_with_ptr = cpp20::bind_front(deref, std::move(ptr));
  auto test_if_movable = std::move(call_with_ptr);

  static_assert(cpp17::is_copy_constructible_v<decltype(ptr)> == false, "");
  static_assert(cpp17::is_copy_constructible_v<decltype(call_with_ptr)> == false, "");
  static_assert(cpp17::is_copy_constructible_v<decltype(test_if_movable)> == false, "");

  EXPECT_EQ(test_if_movable(), 3);
}

TEST(BindFrontTest, MemberPointers) {
  liar lp;
  auto liar_pmf = cpp20::bind_front(&member_pointers::pmf_add_one, lp, 1);
  auto liar_pmd = cpp20::bind_front(&member_pointers::pmd_add_one, lp);
  EXPECT_EQ(liar_pmf(), 3);
  EXPECT_EQ(liar_pmd()(1), 2);
}

}  // namespace
