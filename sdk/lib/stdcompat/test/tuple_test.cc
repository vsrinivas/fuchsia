// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/tuple.h>

#include <array>

#include <gtest/gtest.h>

namespace {
struct silent {};
struct liar {
  int i;
};
struct truther {
  int i;
  int j;
};
}  // namespace

// You are allowed to specialize std::tuple_size and std::tuple_element
namespace std {
template <>
struct tuple_size<liar> : std::integral_constant<size_t, 3> {};
template <>
struct tuple_size<truther> : std::integral_constant<size_t, 2> {};
}  // namespace std

namespace {

template <typename T>
constexpr void test_tuple_size_v() {
  static_assert(cpp17::tuple_size_v<T> == std::tuple_size<T>::value, "");
}

TEST(TupleTest, TupleSizeVMatchesStd) {
  static_assert(cpp17::tuple_size_v<liar> == 3, "");
  static_assert(cpp17::tuple_size_v<truther> == 2, "");
  test_tuple_size_v<liar>();
  test_tuple_size_v<truther>();

  test_tuple_size_v<std::pair<int, int>>();
  test_tuple_size_v<std::pair<void*, std::string>>();

  test_tuple_size_v<std::tuple<>>();
  test_tuple_size_v<std::tuple<int>>();
  test_tuple_size_v<std::tuple<int, int>>();
  test_tuple_size_v<std::tuple<int, int, int>>();
  test_tuple_size_v<std::tuple<std::string, void*, int>>();

  test_tuple_size_v<std::array<int, 5>>();
  test_tuple_size_v<std::array<std::vector<std::string>, 42>>();
}

#if __cpp_lib_type_trait_variable_templates >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <typename T>
constexpr void tuple_size_v_is_alias() {
  static_assert(&cpp17::tuple_size_v<T> == &std::tuple_size_v<T>, "");
}

TEST(TupleTest, TupleSizeIsAliasForStdWhenAvailable) {
  tuple_size_v_is_alias<liar>();
  tuple_size_v_is_alias<truther>();

  tuple_size_v_is_alias<std::pair<int, int>>();
  tuple_size_v_is_alias<std::pair<void*, std::string>>();

  tuple_size_v_is_alias<std::tuple<>>();
  tuple_size_v_is_alias<std::tuple<int>>();
  tuple_size_v_is_alias<std::tuple<int, int>>();
  tuple_size_v_is_alias<std::tuple<int, int, int>>();
  tuple_size_v_is_alias<std::tuple<std::string, void*, int>>();

  tuple_size_v_is_alias<std::array<int, 5>>();
  tuple_size_v_is_alias<std::array<std::vector<std::string>, 42>>();
}

#endif

template <typename T>
constexpr decltype(auto) reduce(T&& only) {
  return std::forward<T>(only);
}

template <typename First, typename Second, typename... Rest>
constexpr decltype(auto) reduce(First&& first, Second&& second, Rest&&... rest) {
  return std::forward<First>(first) +
         reduce(std::forward<Second>(second), std::forward<Rest>(rest)...);
}

constexpr auto test = [](auto&&... args) { return reduce(std::forward<decltype(args)>(args)...); };

struct UserType {
  int var;
};

constexpr double test_heterogeneous(double d, const std::string& s, UserType u) {
  return d + std::stod(s) + u.var;
}

TEST(TupleTest, Apply) {
  static_assert(cpp17::apply(test, std::make_tuple(1)) == 1, "");
  static_assert(cpp17::apply(test, std::make_tuple(1, 2)) == 3, "");
  static_assert(cpp17::apply(test, std::make_tuple(1, 2, 3)) == 6, "");
  static_assert(cpp17::apply(test, std::make_tuple(0.125f, 0.25f, 0.5f, 1.0f)) == 1.875f, "");

  EXPECT_EQ(cpp17::apply(test, std::array<std::string, 4>{"ab", "cd", "ef", "gh"}), "abcdefgh");
  EXPECT_EQ(cpp17::apply(test_heterogeneous, std::make_tuple(1.25, "3.5", UserType{.var = 1})),
            5.75);
}

#if __cpp_lib_apply >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <typename R, typename F, typename T>
constexpr void check_apply_alias() {
  // Needed so that the compiler picks the right overload.
  constexpr R (*cpp17_apply)(F&&, T &&) = &cpp17::apply<F, T>;
  constexpr R (*std_apply)(F&&, T &&) = &std::apply<F, T>;
  static_assert(cpp17_apply == std_apply);
}

TEST(TupleTest, ApplyIsAliasForStdWhenAvailable) {
  check_apply_alias<int, decltype(test), std::tuple<int, int, int>>();
  check_apply_alias<int, decltype(test), std::array<int, 3>>();
  check_apply_alias<double, decltype(test_heterogeneous),
                    std::tuple<double, const char*, UserType>>();
}

#endif  // __cpp_lib_apply >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace
