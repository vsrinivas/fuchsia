// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/future.h>
#include <lib/stdcompat/type_traits.h>

#include <functional>

#include <zxtest/zxtest.h>

#define ASSERT_CRITICAL(expr)                                 \
  do {                                                        \
    if (!(expr)) {                                            \
      printf("Line %u: abort, %s failed\n", __LINE__, #expr); \
      __builtin_abort();                                      \
    }                                                         \
  } while (false)

namespace {

class fake_context final : public fasync::context {
 public:
  fasync::executor& executor() const override { return const_cast<noop_executor&>(executor_); }
  fasync::suspended_task suspend_task() override {
    __builtin_abort();
    return fasync::suspended_task();
  }

 private:
  class noop_executor final : public fasync::executor {
    void schedule(fasync::pending_task&& task) override {}
  };

  noop_executor executor_;
};

template <typename E, typename... Ts>
class capture_result_wrapper {
 public:
  template <typename F>
  decltype(auto) wrap(F&& future) {
    static_assert(cpp17::is_same_v<E, fasync::future_error_t<F>>, "");
    if constexpr (sizeof...(Ts) == 1) {
      static_assert(cpp17::is_same_v<fasync::internal::first_t<Ts...>, fasync::future_value_t<F>>,
                    "");
    }
    return std::forward<F>(future) | fasync::then([this](fit::result<E, Ts...> result) {
             last_result = fasync::ready(std::move(result));
           });
  }

  fasync::try_poll<E, Ts...> last_result = fasync::pending();
};

struct move_only {
  move_only(const move_only&) = delete;
  move_only(move_only&&) = default;
  move_only& operator=(const move_only&) = delete;
  move_only& operator=(move_only&&) = default;
};

void resume_in_a_little_while(fasync::suspended_task task) {
  std::thread([task]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.resume();
  }).detach();
}

fasync::future<> sleep_for_a_little_while() {
  // This is a rather inefficient way to wait for time to pass but it
  // is sufficient for our examples.
  return fasync::make_future([waited = false](fasync::context& context) mutable {
    if (waited)
      return;
    waited = true;
    resume_in_a_little_while(context.suspend_task());
  });
}

// Just a simple test to put the future through its paces.
// Other tests go into more detail to cover the API surface.
TEST(FutureTests, basics) {
  for (int i = 0; i < 5; i++) {
    // Make a future that calculates half the square of a number.
    // Produces an error if the square is odd.
    auto future =
        fasync::make_future([i] {
          // Pretend that squaring numbers is hard and takes time
          // to finish...
          return sleep_for_a_little_while() | fasync::then([i] { return fit::ok(i * i); });
        }) |
        fasync::then([](auto square) -> fit::result<const char*, int> {
          if (square.value() % 2 == 0)
            return fit::ok(square.value() / 2);
          return fit::error("square is odd");
        });

// Needs single_threaded_executor.h (coming)
#if 0
    // Evaluate the future.
    fit::result<const char*, int> result = fasync::block(std::move(future)).value();
    if (i % 2 == 0) {
      EXPECT_TRUE(result.is_ok());
      EXPECT_EQ(i * i / 2, result.value());
    } else {
      EXPECT_TRUE(result.is_error());
      EXPECT_STREQ("square is odd", result.error_value());
    }
#endif
  }
}

TEST(FutureTests, invocation) {
  uint64_t run_count = 0;
  fake_context fake_context;
  fasync::try_future<fit::failed> future(
      [&](fasync::context& context) -> fasync::try_poll<fit::failed> {
        ASSERT_CRITICAL(&context == &fake_context);
        if (++run_count == 2)
          return fasync::ready(fit::ok());
        return fasync::pending();
      });
  // EXPECT_TRUE(future);

  fasync::try_poll<fit::failed> poll = future(fake_context);
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(poll.is_pending());
  // EXPECT_TRUE(future);

  poll = future(fake_context);
  EXPECT_EQ(2, run_count);
  EXPECT_TRUE(poll.output().is_ok());
  // EXPECT_FALSE(future);
}

TEST(FutureTests, assignment_and_swap) {
  fake_context fake_context;

  fasync::future<> empty = fasync::make_future([] {});

  uint64_t run_count = 0;
  fasync::try_future<fit::failed> future(
      [&](fasync::context& context) -> fasync::try_poll<fit::failed> {
        run_count++;
        return fasync::pending();
      });

  fasync::future<> x(std::move(empty));

  fasync::try_future<fit::failed> y(std::move(future));
  static_cast<void>(y(fake_context));
  EXPECT_EQ(1, run_count);

  y = [&](fasync::context& context) -> fasync::try_poll<fit::failed> {
    run_count *= 2;
    return fasync::pending();
  };
  static_cast<void>(y(fake_context));
  EXPECT_EQ(2, run_count);

  x = std::move(y);
  static_cast<void>(x(fake_context));
  EXPECT_EQ(4, run_count);
}

TEST(FutureTests, make_future) {
  fake_context fake_context;

  // Handler signature: void().
  {
    uint64_t run_count = 0;
    auto f = fasync::make_future([&] { run_count++; });
    fasync::poll<> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_ready());
  }

  // Handler signature: fit::result<int, char>().
  {
    uint64_t run_count = 0;
    auto f = fasync::make_future([&]() -> fit::result<char, int> {
      run_count++;
      return fit::ok(42);
    });
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Handler signature: fit::ok<int>().
  {
    uint64_t run_count = 0;
    auto f = fasync::make_future([&] {
      run_count++;
      return fit::ok(42);
    });
    static_assert(fasync::is_future_v<decltype(f)>, "");
    static_assert(fasync::is_try_future_v<decltype(f)>, "");
    static_assert(cpp17::is_same_v<fit::failed, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<fit::failed, int> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  {
    uint64_t run_count = 0;
    auto f = fasync::make_future([&] {
      run_count++;
      return fit::error(42);
    });
    static_assert(cpp17::is_same_v<int, fasync::future_error_t<decltype(f)>>, "");
    fasync::try_poll<int> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ(42, poll.output().error_value());
  }

  // Handler signature: fasync::pending().
  {
    uint64_t run_count = 0;
    auto f = fasync::make_future([&] {
      run_count++;
      return fasync::pending();
    });
    fasync::poll<> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_pending());
  }

  // Handler signature: fasync::unboxed_future<...>.
  {
    uint64_t run_count = 0;
    uint64_t run_count2 = 0;
    auto f = fasync::make_future([&] {
      run_count++;
      return fasync::make_future([&]() -> fasync::try_poll<char, int> {
        if (++run_count2 == 2)
          return fasync::ready(fit::ok(42));
        return fasync::pending();
      });
    });
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(1, run_count2);
    EXPECT_TRUE(poll.is_pending());
    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(2, run_count2);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Handler signature: void(context&).
  {
    uint64_t run_count = 0;
    auto f = fasync::make_future([&](fasync::context& context) {
      ASSERT_CRITICAL(&context == &fake_context);
      run_count++;
    });
    fasync::poll<> poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_ready());
  }
}

// This is a bit lower level than fasync::make_future() in that there's no automatic adaptation of
// the handler type.
TEST(FutureTests, make_future_with_continuation) {
  uint64_t run_count = 0;
  fake_context fake_context;
  auto f = fasync::make_future([&](fasync::context& context) -> fit::result<char, int> {
    ASSERT_CRITICAL(&context == &fake_context);
    run_count++;
    return fit::ok(42);
  });
  static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
  static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");

  fasync::try_poll<char, int> poll = f(fake_context);
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(poll.output().is_ok());
  EXPECT_EQ(42, poll.output().value());
}

TEST(FutureTests, make_try_future) {
  fake_context fake_context;

  // Argument type: fit::result<int, char>
  {
    auto f = fasync::make_try_future(fit::result<char, int>(fit::ok(42)));
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Argument type: fit::result<fit::failed, int> with inferred types
  {
    auto f = fasync::make_ok_future(42);
    static_assert(cpp17::is_same_v<fit::failed, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<fit::failed, int> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Argument type: fit::result<char, int> with explicit types
  {
    auto f = fasync::make_try_future<char, int>(fit::ok(42));
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Argument type: fit::result<char> with inferred types
  {
    auto f = fasync::make_error_future('x');
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    fasync::try_poll<char> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('x', poll.output().error_value());
  }

  // Argument type: fit::result<char, int> with explicit types
  {
    auto f = fasync::make_try_future<char, int>(fit::error('x'));
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('x', poll.output().error_value());
  }

  // Argument type: fit::pending with inferred types
  {
    auto f = fasync::make_pending_future();
    fasync::poll<> poll = f(fake_context);
    EXPECT_TRUE(poll.is_pending());
  }

  // Argument type: fasync::pending with explicit types
  {
    auto f = fasync::make_pending_try_future<char, int>();
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_TRUE(poll.is_pending());
  }
}

TEST(FutureTests, make_ok_future) {
  fake_context fake_context;

  // Argument type: int
  {
    auto f = fasync::make_ok_future(42);
    static_assert(cpp17::is_same_v<fit::failed, fasync::future_error_t<decltype(f)>>, "");
    static_assert(cpp17::is_same_v<int, fasync::future_value_t<decltype(f)>>, "");
    fasync::try_poll<fit::failed, int> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Argument type: none (void)
  {
    auto f = fasync::make_ok_future();
    static_assert(cpp17::is_same_v<fit::failed, fasync::future_error_t<decltype(f)>>, "");
    fasync::try_poll<fit::failed> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_ok());
  }
}

TEST(FutureTests, make_error_future) {
  fake_context fake_context;

  // Argument type: char
  {
    auto f = fasync::make_error_future('x');
    static_assert(cpp17::is_same_v<char, fasync::future_error_t<decltype(f)>>, "");
    fasync::try_poll<char> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('x', poll.output().error_value());
  }

  // Argument type: none (void)
  {
    [[maybe_unused]] auto f = fasync::make_failed_future();
    static_assert(cpp17::is_same_v<fit::failed, fasync::future_error_t<decltype(f)>>, "");
    fasync::try_poll<fit::failed> poll = f(fake_context);
    EXPECT_TRUE(poll.output().is_error());
  }
}

auto make_checked_ok_future(int value) {
  return fasync::make_future([value, count = 0]() mutable -> fit::result<char, int> {
    ASSERT_CRITICAL(count == 0);
    ++count;
    return fit::ok(value);
  });
}

auto make_move_only_future(int value) {
  return fasync::make_future(
      [value, count = 0]() mutable -> fit::result<char, std::unique_ptr<int>> {
        ASSERT_CRITICAL(count == 0);
        ++count;
        return fit::ok(std::make_unique<int>(value));
      });
}

auto make_checked_error_future(char error) {
  return fasync::make_future([error, count = 0]() mutable -> fit::result<char, int> {
    ASSERT_CRITICAL(count == 0);
    ++count;
    return fit::error(error);
  });
}

auto make_delayed_ok_future(int value) {
  return fasync::make_future([value, count = 0]() mutable -> fasync::try_poll<char, int> {
    ASSERT_CRITICAL(count <= 1);
    if (++count == 2)
      return fasync::ready(fit::ok(value));
    return fasync::pending();
  });
}

auto make_delayed_error_future(char error) {
  return fasync::make_future([error, count = 0]() mutable -> fasync::try_poll<char, int> {
    ASSERT_CRITICAL(count <= 1);
    if (++count == 2)
      return fasync::ready(fit::error(error));
    return fasync::pending();
  });
}

// To keep these tests manageable, we only focus on argument type adaptation since return type
// adaptation logic is already covered by |make_future()|  and by the examples.
TEST(FutureTests, then_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: fit::result<fit::failed>(const fit::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto f =
        make_delayed_ok_future(42) |
        fasync::then([&](const fit::result<char, int>& result) -> fasync::try_poll<fit::failed> {
          ASSERT_CRITICAL(result.value() == 42);
          if (++run_count == 2)
            return fasync::ready(fit::ok());
          return fasync::pending();
        });

    fasync::try_poll<fit::failed> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(2, run_count);
    EXPECT_TRUE(poll.output().is_ok());
  }

  // Chaining on ERROR.
  // Handler signature: fit::result<fit::failed>(const fit::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto f =
        make_delayed_error_future('x') |
        fasync::then([&](const fit::result<char, int>& result) -> fasync::try_poll<fit::failed> {
          ASSERT_CRITICAL(result.error_value() == 'x');
          if (++run_count == 2)
            return fasync::ready(fit::ok());
          return fasync::pending();
        });

    fasync::try_poll<fit::failed> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(2, run_count);
    EXPECT_TRUE(poll.output().is_ok());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto f = make_checked_ok_future(42) |
             fasync::then([&](fit::result<char, int>& result) -> fit::result<char, int> {
               run_count++;
               return fit::ok(result.value() + 1);
             }) |
             fasync::then([&](const fit::result<char, int>& result) -> fit::result<char, int> {
               run_count++;
               return fit::ok(result.value() + 1);
             }) |
             fasync::then([&](fasync::context& context,
                              fit::result<char, int>& result) -> fit::result<char, int> {
               ASSERT_CRITICAL(&context == &fake_context);
               run_count++;
               return fit::ok(result.value() + 1);
             }) |
             fasync::then([&](fasync::context& context,
                              const fit::result<char, int>& result) -> fit::result<char, int> {
               ASSERT_CRITICAL(&context == &fake_context);
               run_count++;
               return fit::ok(result.value() + 1);
             });

    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(4, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(46, poll.output().value());
  }
}

TEST(FutureTests, and_then_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: fit::result<fit::failed>(const int&).
  {
    uint64_t run_count = 0;
    auto f = make_delayed_ok_future(42) |
             fasync::and_then([&](const int& value) -> fasync::try_poll<char> {
               ASSERT_CRITICAL(value == 42);
               if (++run_count == 2)
                 return fasync::ready(fit::error('y'));
               return fasync::pending();
             });

    fasync::try_poll<char> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(2, run_count);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('y', poll.output().error_value());
  }

  // Chaining on ERROR.
  // Handler signature: fit::result<fit::failed>(const int&).
  {
    uint64_t run_count = 0;
    auto f = make_delayed_error_future('x') |
             fasync::and_then([&](const int& value) -> fasync::try_poll<char> {
               run_count++;
               return fasync::pending();
             });

    fasync::try_poll<char> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('x', poll.output().error_value());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto f =
        make_checked_ok_future(42) | fasync::and_then([&](int& value) -> fit::result<char, int> {
          run_count++;
          return fit::ok(value + 1);
        }) |
        fasync::and_then([&](const int& value) -> fit::result<char, int> {
          run_count++;
          return fit::ok(value + 1);
        }) |
        fasync::and_then([&](fasync::context& context, int& value) -> fit::result<char, int> {
          ASSERT_CRITICAL(&context == &fake_context);
          run_count++;
          return fit::ok(value + 1);
        }) |
        fasync::and_then([&](fasync::context& context, const int& value) -> fit::result<char, int> {
          ASSERT_CRITICAL(&context == &fake_context);
          run_count++;
          return fit::ok(value + 1);
        });

    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(4, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(46, poll.output().value());
  }
}

TEST(FutureTests, or_else_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: fit::result<fit::failed>(const char&).
  {
    uint64_t run_count = 0;
    auto f = make_delayed_ok_future(42) |
             fasync::or_else([&](const char& error) -> fasync::try_poll<fit::failed, int> {
               run_count++;
               return fasync::pending();
             });

    fasync::try_poll<fit::failed, int> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Chaining on ERROR.
  // Handler signature: fit::result<fit::failed>(const char&).
  {
    uint64_t run_count = 0;
    auto f = make_delayed_error_future('x') |
             fasync::or_else([&](const char& error) -> fasync::try_poll<fit::failed, int> {
               ASSERT_CRITICAL(error == 'x');
               if (++run_count == 2)
                 return fasync::ready(fit::ok(43));
               return fasync::pending();
             });

    fasync::try_poll<fit::failed, int> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(2, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(43, poll.output().value());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto f =
        make_checked_error_future('a') |
        fasync::or_else([&](char& error) -> fit::result<char, int> {
          run_count++;
          return fit::error(static_cast<char>(error + 1));
        }) |
        fasync::or_else([&](const char& error) -> fit::result<char, int> {
          run_count++;
          return fit::error(static_cast<char>(error + 1));
        }) |
        fasync::or_else([&](fasync::context& context, char& error) -> fit::result<char, int> {
          ASSERT_CRITICAL(&context == &fake_context);
          run_count++;
          return fit::error(static_cast<char>(error + 1));
        }) |
        fasync::or_else([&](fasync::context& context, const char& error) -> fit::result<char, int> {
          ASSERT_CRITICAL(&context == &fake_context);
          run_count++;
          return fit::error(static_cast<char>(error + 1));
        });

    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(4, run_count);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('e', poll.output().error_value());
  }
}

TEST(FutureTests, inspect_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: void(const fit::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto f =
        make_delayed_ok_future(42) | fasync::inspect([&](const fit::result<char, int>& result) {
          ASSERT_CRITICAL(result.value() == 42);
          run_count++;
        });

    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }

  // Chaining on ERROR.
  // Handler signature: void(const fit::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto f =
        make_delayed_error_future('x') | fasync::inspect([&](const fit::result<char, int>& result) {
          ASSERT_CRITICAL(result.error_value() == 'x');
          run_count++;
        });

    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(0, run_count);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(poll.output().is_error());
    EXPECT_EQ('x', poll.output().error_value());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto f = make_checked_ok_future(42) |
             fasync::inspect([&](const fit::result<char, int>& result) {
               ASSERT_CRITICAL(result.value() == 42);
               run_count++;
             }) |
             fasync::inspect([&](const fit::result<char, int>& result) { run_count++; }) |
             fasync::inspect([&](fasync::context& context, const fit::result<char, int>& result) {
               ASSERT_CRITICAL(&context == &fake_context);
               run_count++;
             }) |
             fasync::inspect([&](fasync::context& context, const fit::result<char, int>& result) {
               ASSERT_CRITICAL(&context == &fake_context);
               run_count++;
             });

    fasync::try_poll<char, int> poll = f(fake_context);
    EXPECT_EQ(4, run_count);
    EXPECT_TRUE(poll.output().is_ok());
    EXPECT_EQ(42, poll.output().value());
  }
}

TEST(FutureTests, discard_result_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  {
    auto f = make_delayed_ok_future(42) | fasync::discard;
    static_assert(cpp17::is_same_v<void, fasync::future_output_t<decltype(f)>>, "");

    fasync::poll<> poll = f(fake_context);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_TRUE(poll.is_ready());
  }

  // Chaining on ERROR.
  {
    auto f = make_delayed_error_future('x') | fasync::discard;
    static_assert(cpp17::is_same_v<void, fasync::future_output_t<decltype(f)>>, "");

    fasync::poll<> poll = f(fake_context);
    EXPECT_TRUE(poll.is_pending());

    poll = f(fake_context);
    EXPECT_TRUE(poll.is_ready());
  }
}

TEST(FutureTests, wrap_with_combinator) {
  fake_context fake_context;
  capture_result_wrapper<char, int> wrapper;
  uint64_t successor_run_count = 0;

  // Apply a wrapper which steals a future's result th
  auto f = make_delayed_ok_future(42) | fasync::wrap_with(wrapper) |
           fasync::then([&] { successor_run_count++; });

  fasync::poll<> poll = f(fake_context);
  EXPECT_TRUE(poll.is_pending());
  EXPECT_TRUE(wrapper.last_result.is_pending());
  EXPECT_EQ(0, successor_run_count);

  poll = f(fake_context);
  EXPECT_TRUE(poll.is_ready());
  EXPECT_TRUE(wrapper.last_result.output().is_ok());
  EXPECT_EQ(42, wrapper.last_result.output().value());
  EXPECT_EQ(1, successor_run_count);
}

TEST(FutureTests, box_combinator) {
  fake_context fake_context;

  auto f = fasync::make_future([&]() -> fit::result<char, int> { return fit::ok(42); });
  static_assert(!cpp17::is_same_v<fasync::try_future<char, int>, decltype(f)>, "");

  auto q = std::move(f) | fasync::box;
  static_assert(cpp17::is_same_v<fasync::try_future<char, int>, decltype(q)>, "");

  fasync::try_poll<char, int> poll = q(fake_context);
  EXPECT_TRUE(poll.output().is_ok());
  EXPECT_EQ(42, poll.output().value());
}

TEST(FutureTests, join_combinator) {
  fake_context fake_context;

  auto f = fasync::join(make_checked_ok_future(42),
                        make_checked_error_future('x') |
                            fasync::or_else([](const char& error) { return fit::error('y'); }),
                        make_delayed_ok_future(55));

  using output = fasync::future_output_t<decltype(f)>;
  static_assert(cpp17::is_same_v<std::tuple_element_t<0, output>, fit::result<char, int>>, "");
  static_assert(cpp17::is_same_v<std::tuple_element_t<1, output>, fit::result<char, int>>, "");
  static_assert(cpp17::is_same_v<std::tuple_element_t<2, output>, fit::result<char, int>>, "");

  fasync::poll<std::tuple<fit::result<char, int>, fit::result<char, int>, fit::result<char, int>>>
      poll = f(fake_context);
  EXPECT_TRUE(poll.is_pending());

  fasync::poll poll2 = f(fake_context);
  EXPECT_TRUE(poll2.is_ready());
  EXPECT_EQ(42, std::get<0>(poll2.output()).value());
  EXPECT_EQ('y', std::get<1>(poll2.output()).error_value());
  EXPECT_EQ(55, std::get<2>(poll2.output()).value());
}

TEST(FutureTests, join_combinator_move_only_result) {
  fake_context fake_context;

  // Add 1 + 2 to get 3, using a join combinator with a "then" continuation to demonstrate how to
  // optionally return an error.
  auto f = fasync::join(make_move_only_future(1), make_move_only_future(2)) |
           fasync::then([](std::tuple<fit::result<char, std::unique_ptr<int>>,
                                      fit::result<char, std::unique_ptr<int>>>& results)
                            -> fit::result<char, std::unique_ptr<int>> {
             if (std::get<0>(results).is_error() || std::get<1>(results).is_error()) {
               return fit::error('e');
             } else {
               int value = *std::get<0>(results).value() + *std::get<1>(results).value();
               return fit::ok(std::make_unique<int>(value));
             }
           });

  fasync::try_poll<char, std::unique_ptr<int>> poll = f(fake_context);
  EXPECT_TRUE(poll.output().is_ok());
  EXPECT_EQ(3, *poll.output().value());
}

TEST(FutureTests, join_vector_combinator) {
  fake_context fake_context;

  std::vector<fasync::try_future<char, int>> futures;
  futures.push_back(make_checked_ok_future(42));
  futures.push_back(make_checked_error_future('x') |
                    fasync::or_else([](const char& error) { return fit::error('y'); }));
  futures.push_back(make_delayed_ok_future(55));
  futures.push_back(fasync::try_future<char, int>(make_checked_ok_future(42)));
  futures.push_back(fasync::try_future<char, int>(make_checked_error_future('x')) |
                    fasync::or_else([](const char& error) { return fit::error('y'); }));
  futures.push_back(fasync::try_future<char, int>(make_checked_error_future('y')));
  futures.push_back(fasync::try_future<char, int>(make_delayed_ok_future(55)));
  auto f = fasync::join(std::move(futures));

  fasync::poll<std::vector<fit::result<char, int>>> poll = f(fake_context);
  EXPECT_TRUE(poll.is_pending());

  auto poll2 = f(fake_context);
  EXPECT_TRUE(poll2.is_ready());
  EXPECT_EQ(42, poll2.output()[0].value());
  EXPECT_EQ('y', poll2.output()[1].error_value());
  EXPECT_EQ(55, poll2.output()[2].value());
}

// Test predicate which is used internally to improve the quality of compilation errors when an
// invalid continuation type is encountered.
namespace is_future_test {

static_assert(fasync::is_future_v<::fit::function<fasync::poll<>(fasync::context&)>>, "");
static_assert(!fasync::is_future_v<::fit::function<void(fasync::context&)>>, "");
static_assert(!fasync::is_future_v<::fit::function<fasync::poll<>()>>, "");
static_assert(!fasync::is_future_v<void>, "");

[[maybe_unused]] auto continuation_lambda = [](fasync::context&) -> fasync::poll<> {
  return fasync::pending();
};
[[maybe_unused]] auto invalid_lambda = [] {};

static_assert(fasync::is_future_v<decltype(continuation_lambda)>, "");
static_assert(!fasync::is_future_v<decltype(invalid_lambda)>, "");

}  // namespace is_future_test

}  // namespace
