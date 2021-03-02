// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/functional.h>

#include <gtest/gtest.h>

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

}  // namespace
