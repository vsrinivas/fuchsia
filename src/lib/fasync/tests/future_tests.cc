// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/future.h>
#include <lib/zx/status.h>

#include <deque>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <list>
#include <numeric>
#include <set>
#include <string>
#include <type_traits>

#include <zxtest/zxtest.h>

#include "lib/stdcompat/tuple.h"
#include "lib/stdcompat/type_traits.h"
#include "test_utils.h"

using namespace std::literals;

namespace {

struct aggregate {
  int a;
  int b;
};

TEST(FutureTests, InvokeHandler) {
  fasync::testing::immediate_executor executor;
  fasync::context& context = executor.context();

  fasync::internal::invoke_handler(
      [](std::pair<int, int> p) {
        EXPECT_EQ(p.first, 1);
        EXPECT_EQ(p.second, 2);
      },
      context, 1, 2);
  fasync::internal::invoke_handler(
      [](std::pair<int, std::string> p) {
        EXPECT_EQ(p.first, 1);
        EXPECT_STREQ(p.second, "asdf");
      },
      context, 1, "asdf");
  fasync::internal::invoke_handler(
      [](std::tuple<int, int> t) {
        EXPECT_EQ(std::get<0>(t), 1);
        EXPECT_EQ(std::get<1>(t), 2);
      },
      context, 1, 2);
  fasync::internal::invoke_handler(
      [](std::array<int, 2> a) {
        EXPECT_EQ(a[0], 1);
        EXPECT_EQ(a[1], 2);
      },
      context, 1, 2);
  fasync::internal::invoke_handler(
      [](aggregate a) {
        EXPECT_EQ(a.a, 1);
        EXPECT_EQ(a.b, 2);
      },
      context, 1, 2);
}

TEST(FutureTests, Map) {
  {
    [[maybe_unused]] auto pipe =
        fasync::make_value_future<int>(2) | fasync::map([](int& i) { return i + 1; });
  }

  {
    [[maybe_unused]] auto result_pipe =
        fasync::make_try_future<int, int>(fit::success(3)) |
        fasync::map_ok([](int i) { return fit::ok(std::to_string(i)); }) |
        fasync::map_error([](int i) { return fit::as_error(std::to_string(i) + " error"); });

    using result_pipe_t = decltype(result_pipe);
    static_assert(std::is_same_v<fasync::future_value_t<result_pipe_t>, std::string>);
    static_assert(std::is_same_v<fasync::future_error_t<result_pipe_t>, std::string>);
  }

  {
    // You don't have to immediately execute the pipeline; you can store it and move to the executor
    // later.
    // We'll do regular int for one since int is copyable.
    auto pipe = fasync::make_value_future(2) | fasync::map([](int& i) { return i + 1; }) |
                fasync::map([](int& i) { return i + 2; }) |
                fasync::map([](int i) { return i + 3; }) |
                fasync::inspect([](int i) { EXPECT_EQ(i, 8); });

    EXPECT_EQ(std::move(pipe) | fasync::testing::invoke, 8);
  }

  {
    auto pipe = fasync::make_value_future(27) |
                fasync::map([](fasync::context& context, int i) { return i + 1; });
    EXPECT_EQ(std::move(pipe) | fasync::testing::invoke, 28);
  }

  {
    // Calling with an lvalue is kind of uncommon
    auto pipe = fasync::make_value_future(23);
    int result = pipe | fasync::map([](int i) { return i - 13; }) | fasync::testing::invoke;
    EXPECT_EQ(result, 10);

    auto mod = fasync::map([](int i) { return i % 7; });
    result = pipe | mod | fasync::testing::invoke;
    EXPECT_EQ(result, 2);
  }
}

TEST(FutureTests, MapHandlers) {
  {
    auto x = fasync::make_value_future(0) | fasync::map([](fasync::context&) { return 42; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) | fasync::map([](int i) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) | fasync::map([](int& i) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) | fasync::map([](auto i) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) | fasync::map([](auto& i) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) | fasync::map([](auto&& i) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) |
             fasync::map([](fasync::context&, int i) { return i; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) |
             fasync::map([](fasync::context&, int& i) { return i; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) |
             fasync::map([](fasync::context&, auto i) { return i; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) |
             fasync::map([](fasync::context&, auto& i) { return i; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(42) |
             fasync::map([](fasync::context&, auto&& i) { return i; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](std::tuple<int, int, int> t) {
               return std::get<0>(t) + std::get<1>(t) + std::get<2>(t);
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](std::tuple<int, int, int>& t) {
               return std::get<0>(t) + std::get<1>(t) + std::get<2>(t);
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](auto& t) { return std::get<0>(t) + std::get<1>(t) + std::get<2>(t); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x =
        fasync::make_value_future(std::make_tuple(0, 1, 2)) |
        fasync::map([](auto&& t) { return std::get<0>(t) + std::get<1>(t) + std::get<2>(t); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, std::tuple<int, int, int> t) {
               return std::get<0>(t) + std::get<1>(t) + std::get<2>(t);
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, std::tuple<int, int, int>& t) {
               return std::get<0>(t) + std::get<1>(t) + std::get<2>(t);
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, auto& t) {
               return std::get<0>(t) + std::get<1>(t) + std::get<2>(t);
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, auto&& t) {
               return std::get<0>(t) + std::get<1>(t) + std::get<2>(t);
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](int i, int j, int k) { return i + j + k; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](int& i, int j, int k) { return i + j + k; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, int i, int j, int k) { return i + j + k; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, int& i, int j, int k) { return i + j + k; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 3);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, int i, auto...) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 0);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, int i, auto&...) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 0);
  }
  {
    auto x = fasync::make_value_future(std::make_tuple(0, 1, 2)) |
             fasync::map([](fasync::context&, int i, auto&&...) { return i; }) |
             fasync::testing::invoke;
    EXPECT_EQ(x, 0);
  }
// Needs single_threaded_executor.h (coming)
#if 0
  {
    auto x =
        fasync::make_value_future(0) |
        fasync::map([slept = false](fasync::context& context, int i) mutable -> fasync::poll<int> {
          if (!slept) {
            slept = true;
            context.suspend_task().resume();
            return fasync::pending();
          }
          return fasync::done(i);
        }) |
        fasync::block;
    EXPECT_EQ(x.value(), 0);
  }
#endif
}

TEST(FutureTests, MapReturnTypes) {
  {
    // void
    fasync::make_value_future(42) | fasync::map([] {}) | fasync::testing::invoke;
  }
  {
    auto x =
        fasync::make_value_future(0) | fasync::map([] { return 42; }) | fasync::testing::invoke;
    EXPECT_EQ(x, 42);
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map([]() -> fit::result<int, std::string> { return fit::ok("asdf"s); }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "asdf");
  }
  {
    auto x = fasync::make_value_future(0) | fasync::map([] { return fit::ok(42); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_value_future(0) | fasync::map([] { return fit::as_error(42); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_value_future(42) | fasync::map([] { return fasync::pending(); }) |
             fasync::testing::poll;
    EXPECT_EQ(x, fasync::pending());
  }
  {
    auto x = fasync::make_value_future(0) | fasync::map([] { return fasync::done(fit::ok(42)); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x =
        fasync::make_value_future(0) |
        fasync::map([]() -> fasync::try_ready<int, int> { return fasync::done(fit::ok(42)); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_value_future(0) |
             fasync::map([]() -> fasync::try_poll<int, int> { return fasync::done(fit::ok(42)); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
}

TEST(FutureTests, MapOkHandlers) {
  {
    auto x = fasync::make_ok_future(0) |
             fasync::map_ok([](fasync::context&) { return fit::ok(42); }) | fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([](int i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([](int& i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([](auto i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([](auto& i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([](auto&& i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([](fasync::context&, int i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([](fasync::context&, int& i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([](fasync::context&, auto i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([](fasync::context&, auto& i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([](fasync::context&, auto&& i) { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](std::tuple<int, int, int> t) {
          return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](std::tuple<int, int, int>& t) {
          return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok(
            [](auto& t) { return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t)); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok(
            [](auto&& t) { return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t)); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, std::tuple<int, int, int> t) {
          return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, std::tuple<int, int, int>& t) {
          return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, auto& t) {
          return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, auto&& t) {
          return fit::ok(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](int i, int j, int k) { return fit::ok(i + j + k); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](int& i, int j, int k) { return fit::ok(i + j + k); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, int i, int j, int k) { return fit::ok(i + j + k); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, int& i, int j, int k) { return fit::ok(i + j + k); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 3);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, int i, auto...) { return fit::ok(i); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 0);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, int i, auto&...) { return fit::ok(i); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 0);
  }
  {
    auto x =
        fasync::make_try_future<int, std::tuple<int, int, int>>(fit::ok(std::make_tuple(0, 1, 2))) |
        fasync::map_ok([](fasync::context&, int i, auto&&...) { return fit::ok(i); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.value(), 0);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::ok(42)) |
             fasync::map_ok([](int i) -> fit::result<int, int> { return fit::ok(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 42);
  }
  {
    auto x =
        fasync::make_try_future<int, int>(fit::ok(42)) |
        fasync::map_ok([](int i) -> fit::result<int, std::string> { return fit::ok("asdf"); }) |
        fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "asdf");
  }
// Needs single_threaded_executor.h (coming)
#if 0
  {
    auto x = fasync::make_try_future<int, int>(fit::ok(0)) |
             fasync::map_ok([slept = false](fasync::context& context,
                                            int i) mutable -> fasync::try_poll<int, int> {
               if (!slept) {
                 slept = true;
                 context.suspend_task().resume();
                 return fasync::pending();
               }
               return fasync::done(fit::ok(i));
             }) |
             fasync::block;
    EXPECT_EQ(x.value().value(), 0);
  }
#endif
}

TEST(FutureTests, MapOkReturnTypes) {
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([] {}) | fasync::testing::invoke;
    EXPECT_TRUE(x.is_ok());
  }
  {
    auto x =
        fasync::make_ok_future(42) |
        fasync::map_ok([]() -> fit::result<fit::failed, std::string> { return fit::ok("asdf"s); }) |
        fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "asdf");
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([] { return fit::ok("asdf"s); }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "asdf");
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([] { return fit::failed(); }) |
             fasync::testing::invoke;
    EXPECT_TRUE(x.is_error());
  }
  {
    auto x = fasync::make_ok_future(42) | fasync::map_ok([] { return fasync::pending(); }) |
             fasync::testing::poll;
    EXPECT_EQ(x, fasync::pending());
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([]() -> fasync::try_ready<fit::failed, std::string> {
               return fasync::done(fit::ok("asdf"s));
             }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "asdf");
  }
  {
    auto x = fasync::make_ok_future(42) |
             fasync::map_ok([]() -> fasync::try_poll<fit::failed, std::string> {
               return fasync::done(fit::ok("asdf"s));
             }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "asdf");
  }
}

TEST(FutureTests, MapErrorHandlers) {
  {
    auto x = fasync::make_error_future(0) |
             fasync::map_error([](fasync::context&) { return fit::as_error(42); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](int i) { return fit::as_error(i); }) | fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](int& i) { return fit::as_error(i); }) | fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](auto i) { return fit::as_error(i); }) | fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](auto& i) { return fit::as_error(i); }) | fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) | fasync::map_error([](auto&& i) { return fit::ok(); }) |
             fasync::testing::invoke;
    EXPECT_TRUE(x.is_ok());
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](auto&& i) { return fit::as_error(i); }) | fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](fasync::context&, int i) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](fasync::context&, int& i) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](fasync::context&, auto i) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](fasync::context&, auto& i) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([](fasync::context&, auto&& i) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](std::tuple<int, int, int> t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](std::tuple<int, int, int>& t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](auto& t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](auto&& t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](fasync::context&, std::tuple<int, int, int> t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](fasync::context&, std::tuple<int, int, int>& t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](fasync::context&, auto& t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](fasync::context&, auto&& t) {
               return fit::as_error(std::get<0>(t) + std::get<1>(t) + std::get<2>(t));
             }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](int i, int j, int k) { return fit::as_error(i + j + k); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](int& i, int j, int k) { return fit::as_error(i + j + k); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error(
                 [](fasync::context&, int i, int j, int k) { return fit::as_error(i + j + k); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error(
                 [](fasync::context&, int& i, int j, int k) { return fit::as_error(i + j + k); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 3);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](fasync::context&, int i, auto...) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 0);
  }
  {
    auto x = fasync::make_try_future<std::tuple<int, int, int>>(
                 fit::as_error(std::make_tuple(0, 1, 2))) |
             fasync::map_error([](fasync::context&, int i, auto&...) { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 0);
  }
  {
    auto x =
        fasync::make_try_future<std::tuple<int, int, int>>(
            fit::as_error(std::make_tuple(0, 1, 2))) |
        fasync::map_error([](fasync::context&, int i, auto&&...) { return fit::as_error(i); }) |
        fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 0);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::as_error(42)) |
             fasync::map_error([](int i) -> fit::result<int, int> { return fit::as_error(i); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 42);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::as_error(42)) |
             fasync::map_error(
                 [](int i) -> fit::result<std::string, int> { return fit::as_error("asdf"); }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.error_value(), "asdf");
  }
// Needs single_threaded_executor.h (coming)
#if 0
  {
    auto x = fasync::make_try_future<int, int>(fit::as_error(0)) |
             fasync::map_error([slept = false](fasync::context& context,
                                               int i) mutable -> fasync::try_poll<int, int> {
               if (!slept) {
                 slept = true;
                 context.suspend_task().resume();
                 return fasync::pending();
               }
               return fasync::done(fit::as_error(i));
             }) |
             fasync::block;
    EXPECT_EQ(x.value().error_value(), 0);
  }
#endif
}

TEST(FutureTests, MapErrorReturnTypes) {
  {
    auto x = fasync::make_error_future(42) | fasync::map_error([] {}) | fasync::testing::invoke;
    EXPECT_TRUE(x.is_ok());
  }
  {
    auto x =
        fasync::make_error_future(42) |
        fasync::map_error([]() -> fit::result<std::string> { return fit::as_error("asdf"s); }) |
        fasync::testing::invoke;
    EXPECT_STREQ(x.error_value(), "asdf");
  }
  {
    auto x = fasync::make_error_future(42) | fasync::map_error([] { return fit::ok(); }) |
             fasync::testing::invoke;
    EXPECT_TRUE(x.is_ok());
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([] { return fit::as_error("asdf"s); }) | fasync::testing::invoke;
    EXPECT_STREQ(x.error_value(), "asdf");
  }
  {
    auto x = fasync::make_error_future(42) | fasync::map_error([] { return fasync::pending(); }) |
             fasync::testing::poll;
    EXPECT_EQ(x, fasync::pending());
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([]() -> fasync::try_ready<std::string> {
               return fasync::done(fit::as_error("asdf"s));
             }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.error_value(), "asdf");
  }
  {
    auto x = fasync::make_error_future(42) |
             fasync::map_error([]() -> fasync::try_poll<std::string> {
               return fasync::done(fit::as_error("asdf"s));
             }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.error_value(), "asdf");
  }
}

TEST(FutureTests, MapOkError) {
  {
    auto x = fasync::make_try_future<int, int>(fit::ok(42)) |
             fasync::map_ok([](int i) { return fit::ok(i + 1); }) |
             fasync::map_error([](int i) { return fit::as_error(i - 1); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.value(), 43);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::ok(42)) |
             fasync::map_error([](int i) { return fit::as_error(i - 1); }) |
             fasync::map_ok([](int i) { return fit::ok(i + 1); }) | fasync::testing::invoke;
    EXPECT_EQ(x.value(), 43);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::as_error(42)) |
             fasync::map_ok([](int i) { return fit::ok(i + 1); }) |
             fasync::map_error([](int i) { return fit::as_error(i - 1); }) |
             fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 41);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::as_error(42)) |
             fasync::map_error([](int i) { return fit::as_error(i - 1); }) |
             fasync::map_ok([](int i) { return fit::ok(i + 1); }) | fasync::testing::invoke;
    EXPECT_EQ(x.error_value(), 41);
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::ok(42)) |
             fasync::map_ok([](int i) { return fit::ok(std::to_string(i + 1)); }) |
             fasync::map_error([](int i) { return fit::as_error(i - 1); }) |
             fasync::map_ok([](std::string& s) { return fit::ok(s + "asdf"); }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.value(), "43asdf");
  }
  {
    auto x = fasync::make_try_future<int, int>(fit::as_error(42)) |
             fasync::map_error([](int i) { return fit::as_error(std::to_string(i - 1)); }) |
             fasync::map_ok([](int i) { return fit::ok(std::to_string(i + 1)); }) |
             fasync::map_error([](std::string& s) { return fit::as_error(s + "jkl"); }) |
             fasync::testing::invoke;
    EXPECT_STREQ(x.error_value(), "41jkl");
  }
}

TEST(FutureTests, Inspect) {
  {
    fit::result result = fasync::make_try_future<int, int>(fit::error(1)) |
                         fasync::inspect_ok([](const int& i) { FAIL(); }) |
                         fasync::inspect_error([](const int& i) { EXPECT_EQ(i, 1); }) |
                         fasync::testing::invoke;
    EXPECT_EQ(result, (fit::result<int, int>(fit::error(1))));
  }
}

template <size_t Levels, typename T,
          fasync::internal::requires_conditions<cpp17::bool_constant<Levels == 0>> = true>
constexpr auto nest_futures(T&& value) {
  return fasync::make_value_future(std::forward<T>(value));
}

template <size_t Levels, typename T,
          fasync::internal::requires_conditions<cpp17::bool_constant<Levels != 0>> = true>
constexpr auto nest_futures(T&& value) {
  return [value = std::move(value)](fasync::context&) mutable
         -> fasync::poll<decltype(nest_futures<Levels - 1>(std::move(value)))> {
    return fasync::ready(nest_futures<Levels - 1>(std::move(value)));
  };
}

TEST(FutureTests, Flatten) {
  {
    auto unnested = nest_futures<0>(28);
    static_assert(fasync::is_future_v<decltype(unnested)>);
    EXPECT_EQ(fasync::testing::invoke(unnested), 28);
  }

  {
    auto nested = nest_futures<2>(std::string_view("asdfjkl"));
    auto flattened = nested | fasync::flatten | fasync::flatten;
    static_assert(fasync::is_future_v<decltype(nested)>);
    static_assert(fasync::is_future_v<decltype(flattened)>);
    EXPECT_STREQ(fasync::testing::invoke(flattened), std::string_view("asdfjkl"));
  }
}

TEST(FutureTests, FlattenAll) {
  {
    auto unnested = nest_futures<0>(28);
    auto flattened = fasync::flatten_all(unnested);
    static_assert(fasync::is_future_v<decltype(unnested)>);
    static_assert(fasync::is_future_v<decltype(flattened)>);
    EXPECT_EQ(fasync::testing::invoke(flattened), 28);
  }

  {
    auto nested = nest_futures<10>(std::string_view("asdfjkl"));
    auto flattened = nested | fasync::flatten_all;
    static_assert(fasync::is_future_v<decltype(nested)>);
    static_assert(fasync::is_future_v<decltype(flattened)>);
    EXPECT_STREQ(fasync::testing::invoke(flattened), std::string_view("asdfjkl"));
  }
}

TEST(FutureTests, Then) {
  {
    auto i = fasync::make_value_future(9) |
             fasync::then([] { return fasync::make_value_future(10); }) | fasync::testing::invoke;
    EXPECT_EQ(i, 10);
  }

  {
    int i = fasync::make_value_future(0) |
            fasync::then([](int i) { return fasync::make_value_future(i + 1); }) |
            fasync::testing::invoke;
    EXPECT_EQ(i, 1);
  }

  {
    fasync::make_value_future(0) |
        fasync::then([](int i) { return fasync::make_value_future(i + 1); }) |
        fasync::then([](int i) { return i + 1; }) | fasync::then([] { return 3; }) |
        fasync::then([] {}) | fasync::then([] {}) | fasync::testing::invoke;
  }

  {
    fit::result result = fasync::make_try_future<int, int>(fit::error(1)) |
                         fasync::and_then([](int i) { return fit::ok(2); }) |
                         fasync::or_else([](int i) { return fit::as_error(3); }) |
                         fasync::testing::invoke;
    EXPECT_EQ(result.error_value(), 3);
  }

  {
    fit::result result =
        fasync::make_try_future<std::string, std::string>(fit::ok("asdf"s)) |
        fasync::and_then([](auto&& str) { return fit::ok(std::make_tuple(str[0], str[1])); }) |
        fasync::or_else([] {
          ADD_FAILURE("Shouldn't be called; also can't return void.");
          return fit::as_error(nullptr);
        }) |
        fasync::and_then([](aggregate a) { return fit::ok(a); }) | fasync::testing::invoke;
    EXPECT_EQ(result->a, 'a');
    EXPECT_EQ(result->b, 's');
  }

  {
    auto result = fasync::make_try_future<int>(fit::error(2)) | fasync::and_then([] {
                    ADD_FAILURE("and_then shouldn't be called here.");
                    return fit::ok(""s);
                  }) |
                  fasync::or_else([](auto i) { return fit::as_error(std::to_string(i)); }) |
                  fasync::then([](auto result) -> decltype(result) {
                    EXPECT_TRUE(result.is_error());
                    return fit::ok(result.error_value() + "asdf");
                  }) |
                  fasync::testing::invoke;
    // This is a different result type than the one we started with
    static_assert(std::is_same_v<decltype(result), fit::result<std::string, std::string>>);
    EXPECT_STREQ(result.value(), "2asdf");
  }

  {
    constexpr auto returns_future = [] { return fasync::make_try_future<int, int>(fit::error(1)); };
    fit::result<std::string, int> f =
        returns_future() | fasync::or_else([](auto i) { return fit::error(std::to_string(i)); }) |
        fasync::testing::invoke;
    EXPECT_TRUE(f.is_error());
    EXPECT_STREQ(f.error_value(), "1");

    fit::result result = returns_future() | fasync::or_else(returns_future) |
                         fasync::or_else([](int i) { return fit::error(i); }) |
                         fasync::or_else([](int i) { return fit::ok(i); }) |
                         fasync::testing::invoke;
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 1);
  }

  // Invoke a pre-composed pipeline, initially without a future.
  {
    auto f = fasync::make_ok_future();
    auto pipeline = fasync::and_then([] {}) | fasync::or_else([] { ADD_FAILURE(); });
    auto x = f | pipeline | fasync::testing::invoke;
    EXPECT_TRUE(x.is_ok());
  }
}

TEST(FutureTests, Join) {
  {
    std::tuple joined =
        fasync::make_value_future(1) |
        fasync::join_with(fasync::make_value_future(2), fasync::make_value_future(3)) |
        fasync::testing::invoke;
    EXPECT_EQ(joined, std::tuple(1, 2, 3));
  }

  {
    int joined = fasync::join(fasync::make_value_future(0), fasync::make_value_future(1),
                              fasync::make_value_future(2)) |
                 fasync::then([](int i, int j, int k) { return i + j + k; }) |
                 fasync::testing::invoke;
    EXPECT_EQ(joined, 3);
  }

  {
    auto str = fasync::join(fasync::make_value_future("asdf"s), fasync::make_value_future("jkl"s),
                            fasync::make_value_future(0)) |
               fasync::then([](auto&& str1, auto&& str2, int num) {
                 return str1 + str2 + std::to_string(num);
               }) |
               fasync::testing::invoke;
    static_assert(std::is_same_v<decltype(str), std::string>);
    EXPECT_STREQ(str, "asdfjkl0");
  }

  {
    int n = fasync::make_value_future(std::make_tuple(fasync::make_value_future(1),
                                                      fasync::make_value_future(2),
                                                      fasync::make_value_future(3))) |
            fasync::join | fasync::then([](int a, int b, int c) { return a + b + c; }) |
            fasync::testing::invoke;
    EXPECT_EQ(n, 6);
  }

  {
    int j =
        fasync::make_value_future("asdf"sv) |
        fasync::join_with(fasync::make_value_future("jkl"sv), fasync::make_value_future(0)) |
        fasync::then([](auto&& str1, auto&& str2, int n) {
          EXPECT_EQ(n, 0);
          // static_assert(cpp17::is_same_v<decltype(str1), std::string_view&>, "");
          // static_assert(cpp17::is_same_v<decltype(str2), std::string_view&>, "");
          return std::array{1, 2, 3, 4, 5};
        }) |
        fasync::then([](int i1, int i2, int i3, int i4, int i5) {
          return std::array{i1, i1 * i2, i1 * i2 * i3, i1 * i2 * i3 * i4, i1 * i2 * i3 * i4 * i5};
        }) |
        fasync::then([](std::array<int, 5> arr) { return std::reduce(arr.begin(), arr.end()); }) |
        fasync::then([](auto i) { return i; }) | fasync::testing::invoke;
    EXPECT_EQ(j, 1 + 2 + 6 + 24 + 120);  // adding up factorials
  }

  struct agg {
    std::string str1;
    std::string str2;
    size_t s;
  };

  {
    constexpr auto test = [](auto... is) -> std::array<size_t, sizeof...(is)> {
      return std::array{is...};
    };
    constexpr auto test_ctad = [](auto... is) { return std::array{is...}; };
    using arg = std::tuple<size_t, size_t, size_t>;
    using ret = std::array<size_t, 3>;
    static_assert(::fasync::internal::is_invocable_handler_internal_v<decltype(test), arg>);
    static_assert(::fasync::internal::is_invocable_handler_internal_v<decltype(test_ctad), arg>);
    static_assert(::fasync::internal::is_invocable_with_applicable_v<decltype(test), arg>);
    static_assert(::fasync::internal::is_invocable_with_applicable_v<decltype(test_ctad), arg>);
    static_assert(
        cpp17::is_same_v<cpp17::invoke_result_t<decltype(test), size_t, size_t, size_t>, ret>);
    static_assert(
        cpp17::is_same_v<cpp17::invoke_result_t<decltype(test_ctad), size_t, size_t, size_t>, ret>);
    static_assert(
        cpp17::is_same_v<
            decltype(::fasync::internal::invoke_handler_internal(test, std::declval<arg>())), ret>);
    static_assert(cpp17::is_same_v<decltype(::fasync::internal::invoke_handler_internal(
                                       test_ctad, std::declval<arg>())),
                                   ret>);

    int i = fasync::make_value_future("asdf"s) |
            fasync::join_with(fasync::make_value_future("jkl"s), fasync::make_value_future(4u)) |
            fasync::then([](agg&& a) {
              EXPECT_STREQ(a.str1, "asdf");
              EXPECT_STREQ(a.str2, "jkl");
              EXPECT_EQ(a.s, 4);
              return fasync::make_value_future(std::tuple(a.str1.size(), a.str2.size(), a.s));
            }) |
            fasync::then([](auto... is) { return std::array{is...}; }) | fasync::then(test_ctad) |
            fasync::then(
                [](std::array<size_t, 3> arr) { return std::reduce(arr.begin(), arr.end()); }) |
// TODO(schottm): figure out why this is necessary
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
            fasync::then([](auto& i) { return i; }) | fasync::testing::invoke;
#pragma GCC diagnostic pop
    EXPECT_EQ(i, 11);
  }

  {
    std::vector<int> joined_vec =
        fasync::join(fasync::make_value_future(1), fasync::make_value_future(2),
                     fasync::make_value_future(3)) |
        fasync::then([](std::vector<int> vec) {
          vec.push_back(4);
          return vec;
        }) |
        fasync::testing::invoke;
    EXPECT_EQ(joined_vec.size(), 4);
  }
}

template <template <typename, typename> class C>
void test_join_sequence_container() {
  C in = {fasync::make_value_future(0), fasync::make_value_future(1), fasync::make_value_future(2)};
  using value_type = fasync::future_output_t<typename decltype(in)::value_type>;
  using allocator_type = std::allocator<value_type>;
  C<value_type, allocator_type> out = fasync::join(std::move(in)) | fasync::then([](auto&& v) {
                                        v.push_back(3);
                                        return std::move(v);
                                      }) |
                                      fasync::testing::invoke;
  EXPECT_EQ(out.size(), 4);
  EXPECT_EQ(out.front(), 0);
  EXPECT_EQ(out.back(), 3);
}

template <template <typename, typename> class C>
void test_join_container_and_remove() {
  C in = {fasync::make_try_future<int, int>(fit::error(0)),
          fasync::make_try_future<int, int>(fit::ok(1)),
          fasync::make_try_future<int, int>(fit::ok(2))};
  using value_type = fasync::future_output_t<typename decltype(in)::value_type>;
  using allocator_type = std::allocator<value_type>;
  C<value_type, allocator_type> out =
      fasync::join(std::move(in)) | fasync::then([](auto&& v) {
        // std::erase_if is C++20 so we have to do it manually
        auto it = std::remove_if(std::begin(v), std::end(v),
                                 [](auto&& result) { return result.is_error(); });
        v.erase(it, std::end(v));
        return std::move(v);
      }) |
      fasync::testing::invoke;
  EXPECT_EQ(std::size(out), 2);
  EXPECT_EQ(std::begin(out)->value(), 1);
  EXPECT_EQ(std::prev(std::end(out))->value(), 2);
}

TEST(FutureTests, JoinContainer) {
  test_join_sequence_container<std::vector>();
  test_join_sequence_container<std::deque>();
  test_join_sequence_container<std::list>();

  test_join_container_and_remove<std::vector>();
  test_join_container_and_remove<std::deque>();
  test_join_container_and_remove<std::list>();

  fasync::try_future<int, int> arr[] = {fasync::make_try_future<int, int>(fit::error(0)),
                                        fasync::make_try_future<int, int>(fit::ok(1)),
                                        fasync::make_try_future<int, int>(fit::ok(2))};
  auto a = fasync::join(std::move(arr)) | fasync::testing::invoke;
  EXPECT_TRUE(a[0].is_error());

  std::array stdarr = {fasync::make_try_future<int, int>(fit::error(0)),
                       fasync::make_try_future<int, int>(fit::ok(1)),
                       fasync::make_try_future<int, int>(fit::ok(2))};
  auto b = fasync::join(std::move(stdarr)) | fasync::testing::invoke;
  EXPECT_TRUE(b[0].is_error());

  cpp20::span<typename decltype(stdarr)::value_type, 3> static_span = stdarr;
  auto d = fasync::join(static_span) | fasync::testing::invoke;
  EXPECT_TRUE(d[0].is_error());
}

}  // namespace
