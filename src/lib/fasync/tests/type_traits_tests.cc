// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/internal/type_traits.h>
#include <lib/fasync/type_traits.h>

#include <zxtest/zxtest.h>

namespace {

TEST(TypeTraitsTests, IsPoll) {
  // fasync::is_poll
  static_assert(fasync::is_poll_v<fasync::poll<>>, "");
  static_assert(fasync::is_poll_v<fasync::poll<int>>, "");
  static_assert(fasync::is_poll_v<fasync::poll<std::string>>, "");
// Needs poll.h (coming)
#if 0
  static_assert(fasync::is_poll_v<fasync::try_poll<fitx::failed>>, "");
  static_assert(fasync::is_poll_v<fasync::try_poll<std::string, int>>, "");
#endif

  static_assert(!fasync::is_poll_v<fasync::pending>, "");
  static_assert(!fasync::is_poll_v<fasync::ready<>>, "");
  static_assert(!fasync::is_poll_v<fasync::ready<int>>, "");
  static_assert(!fasync::is_poll_v<fitx::failed>, "");
  static_assert(!fasync::is_poll_v<void>, "");
  static_assert(!fasync::is_poll_v<int>, "");
  static_assert(!fasync::is_poll_v<std::string>, "");

// Needs poll.h (coming)
#if 0
  // fasync::is_void_poll
  static_assert(fasync::is_void_poll_v<fasync::poll<>>, "");
  static_assert(!fasync::is_void_poll_v<fasync::poll<int>>, "");

  // fasync::is_try_poll
  static_assert(fasync::is_try_poll_v<fasync::try_poll<fitx::failed>>, "");
  static_assert(fasync::is_try_poll_v<fasync::try_poll<std::string, int>>, "");
  static_assert(!fasync::is_try_poll_v<fasync::poll<>>, "");
  static_assert(!fasync::is_try_poll_v<fasync::poll<int>>, "");
#endif
}

TEST(TypeTraitsTests, IsReady) {
  // fasync::is_ready
  static_assert(fasync::is_ready_v<fasync::ready<>>, "");
  static_assert(fasync::is_ready_v<fasync::ready<int>>, "");
  static_assert(fasync::is_ready_v<fasync::ready<std::string>>, "");
// Needs poll.h (coming)
#if 0
  static_assert(fasync::is_ready_v<fasync::try_ready<fitx::failed>>, "");
  static_assert(fasync::is_ready_v<fasync::try_ready<std::string, int>>, "");
#endif

  static_assert(!fasync::is_ready_v<fasync::pending>, "");
  static_assert(!fasync::is_ready_v<fasync::poll<>>, "");
  static_assert(!fasync::is_ready_v<fasync::poll<int>>, "");
  static_assert(!fasync::is_ready_v<fitx::failed>, "");
  static_assert(!fasync::is_ready_v<void>, "");
  static_assert(!fasync::is_ready_v<int>, "");
  static_assert(!fasync::is_ready_v<std::string>, "");
}

// Needs poll.h (coming)
#if 0
fasync::poll<> function_future(fasync::context&) { return fasync::rdy(); }

TEST(TypeTraitsTests, IsFuture) {
  constexpr auto pending_poll = [](fasync::context&) -> fasync::poll<> {
    return fasync::pending();
  };
  constexpr auto ready_poll = [](fasync::context&) -> fasync::poll<> { return fasync::rdy(); };
  constexpr auto pending_with_value = [](fasync::context&) -> fasync::poll<int> {
    return fasync::pending();
  };
  constexpr auto ready_with_value = [](fasync::context&) -> fasync::poll<int> {
    return fasync::rdy(42);
  };
  constexpr auto try_pending = [](fasync::context&) -> fasync::try_poll<fitx::failed> {
    return fasync::pending();
  };
  constexpr auto try_ready = [](fasync::context&) -> fasync::try_poll<fitx::failed> {
    return fasync::rdy(fitx::ok());
  };

  struct functor {
    fasync::poll<> operator()(fasync::context&) { return fasync::rdy(); }
  };

  constexpr auto no_context = []() -> fasync::poll<> { return fasync::pending(); };
  constexpr auto no_poll = [](fasync::context&) { return fasync::pending(); };
  constexpr auto neither = [] { return fasync::pending(); };

  // fasync::is_future
  static_assert(fasync::is_future_v<decltype(pending_poll)>, "");
  static_assert(fasync::is_future_v<decltype(ready_poll)>, "");
  static_assert(fasync::is_future_v<decltype(pending_with_value)>, "");
  static_assert(fasync::is_future_v<decltype(ready_with_value)>, "");
  static_assert(fasync::is_future_v<decltype(try_pending)>, "");
  static_assert(fasync::is_future_v<decltype(try_ready)>, "");
  static_assert(fasync::is_future_v<decltype(functor)>, "");
  static_assert(fasync::is_future_v<decltype(function_future)>, "");
  static_assert(fasync::is_future_v<decltype(std::ref(function_future))>, "");

  static_assert(!fasync::is_future_v<decltype(no_context)>, "");
  static_assert(!fasync::is_future_v<decltype(no_poll)>, "");
  static_assert(!fasync::is_future_v<decltype(neither)>, "");

  // fasync::is_void_future
  static_assert(fasync::is_void_future_v<decltype(pending_poll)>, "");
  static_assert(fasync::is_void_future_v<decltype(ready_poll)>, "");
  static_assert(fasync::is_void_future_v<decltype(functor)>, "");
  static_assert(fasync::is_void_future_v<decltype(function_future)>, "");
  static_assert(fasync::is_void_future_v<decltype(std::ref(function_future))>, "");

  static_assert(!fasync::is_void_future_v<decltype(pending_with_value)>, "");
  static_assert(!fasync::is_void_future_v<decltype(ready_with_value)>, "");
  static_assert(!fasync::is_void_future_v<decltype(try_pending)>, "");
  static_assert(!fasync::is_void_future_v<decltype(try_ready)>, "");

  // fasync::is_try_future
  static_assert(fasync::is_try_future_v<decltype(try_pending)>, "");
  static_assert(fasync::is_try_future_v<decltype(try_ready)>, "");

  static_assert(!fasync::is_try_future_v<decltype(pending_poll)>, "");
  static_assert(!fasync::is_try_future_v<decltype(ready_poll)>, "");
  static_assert(!fasync::is_try_future_v<decltype(pending_with_value)>, "");
  static_assert(!fasync::is_try_future_v<decltype(ready_with_value)>, "");
  static_assert(!fasync::is_try_future_v<decltype(functor)>, "");
  static_assert(!fasync::is_try_future_v<decltype(function_future)>, "");
  static_assert(!fasync::is_try_future_v<decltype(std::ref(function_future))>, "");
}
#endif

TEST(TypeTraitsTests, IsValue) {
  // Needs poll.h (coming)
  // fasync::internal::is_value_result
  // fasync::internal::is_value_try_poll
  // fasync::internal::is_value_try_future
}

struct functor_first {
  template <typename T>
  T operator()(T&& t, int i) {
    return std::forward<T>(t);
  }
};

struct functor_second {
  template <typename T>
  int operator()(int i, T&& t) {
    return i;
  }
};

struct functor_variadic {
  template <typename... Ts>
  std::tuple<Ts...> operator()(Ts&&... ts) {
    return std::make_tuple(std::forward<Ts>(ts)...);
  }
};

TEST(TypeTraitsTests, FirstParamIsGeneric) {
  constexpr auto regular_auto_first = [](auto x) { return x; };
  constexpr auto ref_auto_first = [](auto& x) { return x; };
  constexpr auto const_ref_auto_first = [](const auto& x) { return x; };
  constexpr auto rvalue_ref_auto_first = [](auto&& x) { return x; };
  constexpr auto const_rvalue_ref_auto_first = [](const auto&& x) { return x; };

  constexpr auto regular_auto_variadic = [](auto... xs) { return std::make_tuple(xs...); };
  constexpr auto ref_auto_variadic = [](auto&... xs) { return std::make_tuple(xs...); };
  constexpr auto const_ref_auto_variadic = [](const auto&... xs) { return std::make_tuple(xs...); };
  constexpr auto rvalue_ref_auto_variadic = [](auto&&... xs) { return std::make_tuple(xs...); };
  constexpr auto const_rvalue_ref_auto_variadic = [](const auto&&... xs) {
    return std::make_tuple(xs...);
  };

  constexpr auto regular_auto_second = [](int i, auto x) { return x; };
  constexpr auto ref_auto_second = [](int i, auto& x) { return x; };
  constexpr auto const_ref_auto_second = [](int i, const auto& x) { return x; };
  constexpr auto rvalue_ref_auto_second = [](int i, auto&& x) { return x; };
  constexpr auto const_rvalue_ref_auto_second = [](int i, const auto&& x) { return x; };

  static_assert(fasync::internal::first_param_is_generic_v<decltype(regular_auto_first), int>, "");
  static_assert(fasync::internal::first_param_is_generic_v<decltype(ref_auto_first), int>, "");
  static_assert(fasync::internal::first_param_is_generic_v<decltype(const_ref_auto_first), int>,
                "");
  static_assert(fasync::internal::first_param_is_generic_v<decltype(rvalue_ref_auto_first), int>,
                "");
  static_assert(
      fasync::internal::first_param_is_generic_v<decltype(const_rvalue_ref_auto_first), int>, "");

  static_assert(
      fasync::internal::first_param_is_generic_v<decltype(regular_auto_variadic), int, int>, "");
  static_assert(fasync::internal::first_param_is_generic_v<decltype(ref_auto_variadic), int&, int&>,
                "");
  static_assert(
      fasync::internal::first_param_is_generic_v<decltype(const_ref_auto_variadic), int, int>, "");
  static_assert(
      fasync::internal::first_param_is_generic_v<decltype(rvalue_ref_auto_variadic), int, int>, "");
  static_assert(fasync::internal::first_param_is_generic_v<decltype(const_rvalue_ref_auto_variadic),
                                                           int, int>,
                "");

  static_assert(
      !fasync::internal::first_param_is_generic_v<decltype(regular_auto_second), int, int>, "");
  static_assert(!fasync::internal::first_param_is_generic_v<decltype(ref_auto_second), int, int>,
                "");
  static_assert(
      !fasync::internal::first_param_is_generic_v<decltype(const_ref_auto_second), int, int>, "");
  static_assert(
      !fasync::internal::first_param_is_generic_v<decltype(rvalue_ref_auto_second), int, int>, "");
  static_assert(
      !fasync::internal::first_param_is_generic_v<decltype(const_rvalue_ref_auto_second), int, int>,
      "");
}

TEST(TypeTraitsTests, IsApplicable) {
  // fasync::internal::has_tuple_size
  static_assert(fasync::internal::has_tuple_size_v<std::tuple<>>, "");
  static_assert(fasync::internal::has_tuple_size_v<std::tuple<int, int, int>>, "");
  static_assert(fasync::internal::has_tuple_size_v<std::array<int, 3>>, "");

  static_assert(!fasync::internal::has_tuple_size_v<int>, "");
  static_assert(!fasync::internal::has_tuple_size_v<std::vector<int>>, "");

  // fasync::internal::is_applicable
  static_assert(fasync::internal::is_applicable_v<std::tuple<>>, "");
  static_assert(fasync::internal::is_applicable_v<std::tuple<int, int, int>>, "");
  static_assert(fasync::internal::is_applicable_v<std::array<int, 3>>, "");

  static_assert(!fasync::internal::is_applicable_v<int>, "");
  static_assert(!fasync::internal::is_applicable_v<std::vector<int>>, "");

  // fasync::internal::is_applicable_to
  constexpr auto make_tuple = [](auto&&... xs) {
    return std::make_tuple(std::forward<decltype(xs)>(xs)...);
  };
  constexpr auto concat3 = [](std::string a, std::string b, std::string c) { return a + b + c; };

  static_assert(fasync::internal::is_applicable_to_v<decltype(make_tuple), std::tuple<>>, "");
  static_assert(
      fasync::internal::is_applicable_to_v<decltype(make_tuple), std::tuple<int, int, int>>, "");
  static_assert(fasync::internal::is_applicable_to_v<decltype(make_tuple), std::array<int, 3>>, "");
  static_assert(
      fasync::internal::is_applicable_to_v<decltype(concat3),
                                           std::tuple<std::string, std::string, std::string>>,
      "");
  static_assert(fasync::internal::is_applicable_to_v<decltype(concat3), std::array<std::string, 3>>,
                "");

  static_assert(!fasync::internal::is_applicable_to_v<decltype(concat3), std::tuple<>>, "");
  static_assert(!fasync::internal::is_applicable_to_v<decltype(concat3), std::tuple<int, int, int>>,
                "");
  static_assert(!fasync::internal::is_applicable_to_v<decltype(concat3), std::array<int, 3>>, "");

// Needs poll.h (coming)
#if 0
  // fasync::internal::is_future_applicable
  struct functor {
    fasync::poll<> operator()(fasync::context&) { return fasync::rdy(); }
  };

  static_assert(fasync::internal::is_future_applicable_v<std::tuple<functor, functor, functor>>,
                "");

  static_assert(!fasync::internal::is_future_applicable_v<std::tuple<int, int, int>>, "");
#endif
}

}  // namespace
