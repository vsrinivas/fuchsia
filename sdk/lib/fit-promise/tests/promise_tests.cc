// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>

#include <functional>

#include <zxtest/zxtest.h>

#include "examples/utils.h"
#include "unittest_utils.h"

namespace {

class fake_context : public fpromise::context {
 public:
  fpromise::executor* executor() const override { ASSERT_CRITICAL(false); }
  fpromise::suspended_task suspend_task() override { ASSERT_CRITICAL(false); }
};

template <typename V = void, typename E = void>
class capture_result_wrapper {
 public:
  template <typename Promise>
  decltype(auto) wrap(Promise promise) {
    static_assert(std::is_same<V, typename Promise::value_type>::value, "");
    static_assert(std::is_same<E, typename Promise::error_type>::value, "");
    ASSERT_CRITICAL(promise);
    return promise.then(
        [this](fpromise::result<V, E>& result) { last_result = std::move(result); });
  }

  fpromise::result<V, E> last_result;
};

struct move_only {
  move_only(const move_only&) = delete;
  move_only(move_only&&) = default;
  move_only& operator=(const move_only&) = delete;
  move_only& operator=(move_only&&) = default;
};

// Just a simple test to put the promise through its paces.
// Other tests go into more detail to cover the API surface.
TEST(PromiseTests, basics) {
  for (int i = 0; i < 5; i++) {
    // Make a promise that calculates half the square of a number.
    // Produces an error if the square is odd.
    auto promise =
        fpromise::make_promise([i] {
          // Pretend that squaring numbers is hard and takes time
          // to finish...
          return utils::sleep_for_a_little_while().then(
              [i](const fpromise::result<>&) { return fpromise::ok(i * i); });
        }).then([](const fpromise::result<int>& square) -> fpromise::result<int, const char*> {
          if (square.value() % 2 == 0)
            return fpromise::ok(square.value() / 2);
          return fpromise::error("square is odd");
        });

    // Evaluate the promise.
    fpromise::result<int, const char*> result = fpromise::run_single_threaded(std::move(promise));
    if (i % 2 == 0) {
      EXPECT_TRUE(result.is_ok());
      EXPECT_EQ(i * i / 2, result.value());
    } else {
      EXPECT_TRUE(result.is_error());
      EXPECT_STREQ("square is odd", result.error());
    }
  }
}

// An empty promise has no continuation.
// We can't do a lot with it but we can check for emptyness.
TEST(PromiseTests, empty_promise) {
  {
    fpromise::promise<> promise;
    EXPECT_FALSE(promise);
  }

  {
    fpromise::promise<> promise(nullptr);
    EXPECT_FALSE(promise);
  }

  {
    fit::function<fpromise::result<>(fpromise::context&)> f;
    fpromise::promise<> promise(std::move(f));
    EXPECT_FALSE(promise);
  }

  {
    std::function<fpromise::result<>(fpromise::context&)> f;
    fpromise::promise<> promise(std::move(f));
    EXPECT_FALSE(promise);
  }
}

TEST(PromiseTests, invocation) {
  uint64_t run_count = 0;
  fake_context fake_context;
  fpromise::promise<> promise([&](fpromise::context& context) -> fpromise::result<> {
    ASSERT_CRITICAL(&context == &fake_context);
    if (++run_count == 2)
      return fpromise::ok();
    return fpromise::pending();
  });
  EXPECT_TRUE(promise);

  fpromise::result<> result = promise(fake_context);
  EXPECT_EQ(1, run_count);
  EXPECT_EQ(fpromise::result_state::pending, result.state());
  EXPECT_TRUE(promise);

  result = promise(fake_context);
  EXPECT_EQ(2, run_count);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_FALSE(promise);
}

TEST(PromiseTests, take_continuation) {
  uint64_t run_count = 0;
  fake_context fake_context;
  fpromise::promise<> promise([&](fpromise::context& context) -> fpromise::result<> {
    ASSERT_CRITICAL(&context == &fake_context);
    run_count++;
    return fpromise::pending();
  });
  EXPECT_TRUE(promise);

  fit::function<fpromise::result<>(fpromise::context&)> f = promise.take_continuation();
  EXPECT_FALSE(promise);
  EXPECT_EQ(0, run_count);

  fpromise::result<> result = f(fake_context);
  EXPECT_EQ(1, run_count);
  EXPECT_EQ(fpromise::result_state::pending, result.state());
}

TEST(PromiseTests, assignment_and_swap) {
  fake_context fake_context;

  fpromise::promise<> empty;
  EXPECT_FALSE(empty);

  uint64_t run_count = 0;
  fpromise::promise<> promise([&](fpromise::context& context) -> fpromise::result<> {
    run_count++;
    return fpromise::pending();
  });
  EXPECT_TRUE(promise);

  fpromise::promise<> x(std::move(empty));
  EXPECT_FALSE(x);

  fpromise::promise<> y(std::move(promise));
  EXPECT_TRUE(y);
  y(fake_context);
  EXPECT_EQ(1, run_count);

  x.swap(y);
  EXPECT_TRUE(x);
  EXPECT_FALSE(y);
  x(fake_context);
  EXPECT_EQ(2, run_count);

  x.swap(x);
  EXPECT_TRUE(x);
  x(fake_context);
  EXPECT_EQ(3, run_count);

  y.swap(y);
  EXPECT_FALSE(y);

  x = nullptr;
  EXPECT_FALSE(x);

  y = [&](fpromise::context& context) -> fpromise::result<> {
    run_count *= 2;
    return fpromise::pending();
  };
  EXPECT_TRUE(y);
  y(fake_context);
  EXPECT_EQ(6, run_count);

  x = std::move(y);
  EXPECT_TRUE(x);
  EXPECT_FALSE(y);
  x(fake_context);
  EXPECT_EQ(12, run_count);

  x = std::move(y);
  EXPECT_FALSE(x);
}

TEST(PromiseTests, comparison_with_nullptr) {
  {
    fpromise::promise<> promise;
    EXPECT_TRUE(promise == nullptr);
    EXPECT_TRUE(nullptr == promise);
    EXPECT_FALSE(promise != nullptr);
    EXPECT_FALSE(nullptr != promise);
  }

  {
    fpromise::promise<> promise(
        [&](fpromise::context& context) -> fpromise::result<> { return fpromise::pending(); });
    EXPECT_FALSE(promise == nullptr);
    EXPECT_FALSE(nullptr == promise);
    EXPECT_TRUE(promise != nullptr);
    EXPECT_TRUE(nullptr != promise);
  }
}

TEST(PromiseTests, make_promise) {
  fake_context fake_context;

  // Handler signature: void().
  {
    uint64_t run_count = 0;
    auto p = fpromise::make_promise([&] { run_count++; });
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_FALSE(p);
  }

  // Handler signature: fpromise::result<int, char>().
  {
    uint64_t run_count = 0;
    auto p = fpromise::make_promise([&]() -> fpromise::result<int, char> {
      run_count++;
      return fpromise::ok(42);
    });
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
    EXPECT_FALSE(p);
  }

  // Handler signature: fpromise::ok<int>().
  {
    uint64_t run_count = 0;
    auto p = fpromise::make_promise([&] {
      run_count++;
      return fpromise::ok(42);
    });
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<int, void> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
    EXPECT_FALSE(p);
  }

  // Handler signature: fpromise::error<int>().
  {
    uint64_t run_count = 0;
    auto p = fpromise::make_promise([&] {
      run_count++;
      return fpromise::error(42);
    });
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<int, decltype(p)::error_type>::value, "");
    fpromise::result<void, int> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ(42, result.error());
    EXPECT_FALSE(p);
  }

  // Handler signature: fpromise::pending().
  {
    uint64_t run_count = 0;
    auto p = fpromise::make_promise([&] {
      run_count++;
      return fpromise::pending();
    });
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());
    EXPECT_TRUE(p);
  }

  // Handler signature: fpromise::promise_impl<...>.
  {
    uint64_t run_count = 0;
    uint64_t run_count2 = 0;
    auto p = fpromise::make_promise([&] {
      run_count++;
      return fpromise::make_promise([&]() -> fpromise::result<int, char> {
        if (++run_count2 == 2)
          return fpromise::ok(42);
        return fpromise::pending();
      });
    });
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(1, run_count2);
    EXPECT_EQ(fpromise::result_state::pending, result.state());
    EXPECT_TRUE(p);
    result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(2, run_count2);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
    EXPECT_FALSE(p);
  }

  // Handler signature: void(context&).
  {
    uint64_t run_count = 0;
    auto p = fpromise::make_promise([&](fpromise::context& context) {
      ASSERT_CRITICAL(&context == &fake_context);
      run_count++;
    });
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<> result = p(fake_context);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_FALSE(p);
  }
}

// This is a bit lower level than fpromise::make_promise() in that there's
// no automatic adaptation of the handler type.
TEST(PromiseTests, make_promise_with_continuation) {
  uint64_t run_count = 0;
  fake_context fake_context;
  auto p = fpromise::make_promise_with_continuation(
      [&](fpromise::context& context) -> fpromise::result<int, char> {
        ASSERT_CRITICAL(&context == &fake_context);
        run_count++;
        return fpromise::ok(42);
      });
  static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
  static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
  EXPECT_TRUE(p);

  fpromise::result<int, char> result = p(fake_context);
  EXPECT_EQ(1, run_count);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_EQ(42, result.value());
  EXPECT_FALSE(p);
}

TEST(PromiseTests, make_result_promise) {
  fake_context fake_context;

  // Argument type: fpromise::result<int, char>
  {
    auto p = fpromise::make_result_promise(fpromise::result<int, char>(fpromise::ok(42)));
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // Argument type: fpromise::ok_result<int> with inferred types
  {
    auto p = fpromise::make_result_promise(fpromise::ok(42));
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<int, void> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // Argument type: fpromise::ok_result<int> with explicit types
  {
    auto p = fpromise::make_result_promise<int, char>(fpromise::ok(42));
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // Argument type: fpromise::error_result<char> with inferred types
  {
    auto p = fpromise::make_result_promise(fpromise::error('x'));
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<void, char> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('x', result.error());
  }

  // Argument type: fpromise::error_result<char> with explicit types
  {
    auto p = fpromise::make_result_promise<int, char>(fpromise::error('x'));
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('x', result.error());
  }

  // Argument type: fpromise::pending_result with inferred types
  {
    auto p = fpromise::make_result_promise(fpromise::pending());
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<void, void> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::pending, result.state());
  }

  // Argument type: fpromise::pending_result with explicit types
  {
    auto p = fpromise::make_result_promise<int, char>(fpromise::pending());
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::pending, result.state());
  }
}

TEST(PromiseTests, make_ok_promise) {
  fake_context fake_context;

  // Argument type: int
  {
    auto p = fpromise::make_ok_promise(42);
    static_assert(std::is_same<int, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<int, void> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // Argument type: none (void)
  {
    auto p = fpromise::make_ok_promise();
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<void, void> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
  }
}

TEST(PromiseTests, make_error_promise) {
  fake_context fake_context;

  // Argument type: int
  {
    auto p = fpromise::make_error_promise('x');
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<char, decltype(p)::error_type>::value, "");
    fpromise::result<void, char> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('x', result.error());
  }

  // Argument type: none (void)
  {
    auto p = fpromise::make_error_promise();
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");
    fpromise::result<void, void> result = p(fake_context);
    EXPECT_EQ(fpromise::result_state::error, result.state());
  }
}

auto make_checked_ok_promise(int value) {
  return fpromise::make_promise([value, count = 0]() mutable -> fpromise::result<int, char> {
    ASSERT_CRITICAL(count == 0);
    ++count;
    return fpromise::ok(value);
  });
}

auto make_move_only_promise(int value) {
  return fpromise::make_promise(
      [value, count = 0]() mutable -> fpromise::result<std::unique_ptr<int>, char> {
        ASSERT_CRITICAL(count == 0);
        ++count;
        return fpromise::ok(std::make_unique<int>(value));
      });
}

auto make_checked_error_promise(char error) {
  return fpromise::make_promise([error, count = 0]() mutable -> fpromise::result<int, char> {
    ASSERT_CRITICAL(count == 0);
    ++count;
    return fpromise::error(error);
  });
}

auto make_delayed_ok_promise(int value) {
  return fpromise::make_promise([value, count = 0]() mutable -> fpromise::result<int, char> {
    ASSERT_CRITICAL(count <= 1);
    if (++count == 2)
      return fpromise::ok(value);
    return fpromise::pending();
  });
}

auto make_delayed_error_promise(char error) {
  return fpromise::make_promise([error, count = 0]() mutable -> fpromise::result<int, char> {
    ASSERT_CRITICAL(count <= 1);
    if (++count == 2)
      return fpromise::error(error);
    return fpromise::pending();
  });
}

// To keep these tests manageable, we only focus on argument type adaptation
// since return type adaptation logic is already covered by |make_promise()|
// and by the examples.
TEST(PromiseTests, then_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: fpromise::result<>(const fpromise::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto p = make_delayed_ok_promise(42).then(
        [&](const fpromise::result<int, char>& result) -> fpromise::result<> {
          ASSERT_CRITICAL(result.value() == 42);
          if (++run_count == 2)
            return fpromise::ok();
          return fpromise::pending();
        });

    fpromise::result<> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(2, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
  }

  // Chaining on ERROR.
  // Handler signature: fpromise::result<>(const fpromise::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto p = make_delayed_error_promise('x').then(
        [&](const fpromise::result<int, char>& result) -> fpromise::result<> {
          ASSERT_CRITICAL(result.error() == 'x');
          if (++run_count == 2)
            return fpromise::ok();
          return fpromise::pending();
        });

    fpromise::result<> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(2, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto p =
        make_checked_ok_promise(42)
            .then([&](fpromise::result<int, char>& result) -> fpromise::result<int, char> {
              run_count++;
              return fpromise::ok(result.value() + 1);
            })
            .then([&](const fpromise::result<int, char>& result) -> fpromise::result<int, char> {
              run_count++;
              return fpromise::ok(result.value() + 1);
            })
            .then([&](fpromise::context& context,
                      fpromise::result<int, char>& result) -> fpromise::result<int, char> {
              ASSERT_CRITICAL(&context == &fake_context);
              run_count++;
              return fpromise::ok(result.value() + 1);
            })
            .then([&](fpromise::context& context,
                      const fpromise::result<int, char>& result) -> fpromise::result<int, char> {
              ASSERT_CRITICAL(&context == &fake_context);
              run_count++;
              return fpromise::ok(result.value() + 1);
            });

    fpromise::result<int, char> result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(4, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(46, result.value());
  }
}

TEST(PromiseTests, and_then_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: fpromise::result<>(const int&).
  {
    uint64_t run_count = 0;
    auto p =
        make_delayed_ok_promise(42).and_then([&](const int& value) -> fpromise::result<void, char> {
          ASSERT_CRITICAL(value == 42);
          if (++run_count == 2)
            return fpromise::error('y');
          return fpromise::pending();
        });

    fpromise::result<void, char> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(2, run_count);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('y', result.error());
  }

  // Chaining on ERROR.
  // Handler signature: fpromise::result<>(const int&).
  {
    uint64_t run_count = 0;
    auto p = make_delayed_error_promise('x').and_then(
        [&](const int& value) -> fpromise::result<void, char> {
          run_count++;
          return fpromise::pending();
        });

    fpromise::result<void, char> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('x', result.error());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto p =
        make_checked_ok_promise(42)
            .and_then([&](int& value) -> fpromise::result<int, char> {
              run_count++;
              return fpromise::ok(value + 1);
            })
            .and_then([&](const int& value) -> fpromise::result<int, char> {
              run_count++;
              return fpromise::ok(value + 1);
            })
            .and_then([&](fpromise::context& context, int& value) -> fpromise::result<int, char> {
              ASSERT_CRITICAL(&context == &fake_context);
              run_count++;
              return fpromise::ok(value + 1);
            })
            .and_then(
                [&](fpromise::context& context, const int& value) -> fpromise::result<int, char> {
                  ASSERT_CRITICAL(&context == &fake_context);
                  run_count++;
                  return fpromise::ok(value + 1);
                });

    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(4, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(46, result.value());
    EXPECT_FALSE(p);
  }
}

TEST(PromiseTests, or_else_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: fpromise::result<>(const char&).
  {
    uint64_t run_count = 0;
    auto p = make_delayed_ok_promise(42).or_else([&](const char& error) -> fpromise::result<int> {
      run_count++;
      return fpromise::pending();
    });

    fpromise::result<int> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // Chaining on ERROR.
  // Handler signature: fpromise::result<>(const char&).
  {
    uint64_t run_count = 0;
    auto p =
        make_delayed_error_promise('x').or_else([&](const char& error) -> fpromise::result<int> {
          ASSERT_CRITICAL(error == 'x');
          if (++run_count == 2)
            return fpromise::ok(43);
          return fpromise::pending();
        });

    fpromise::result<int> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(2, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(43, result.value());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto p =
        make_checked_error_promise('a')
            .or_else([&](char& error) -> fpromise::result<int, char> {
              run_count++;
              return fpromise::error(static_cast<char>(error + 1));
            })
            .or_else([&](const char& error) -> fpromise::result<int, char> {
              run_count++;
              return fpromise::error(static_cast<char>(error + 1));
            })
            .or_else([&](fpromise::context& context, char& error) -> fpromise::result<int, char> {
              ASSERT_CRITICAL(&context == &fake_context);
              run_count++;
              return fpromise::error(static_cast<char>(error + 1));
            })
            .or_else(
                [&](fpromise::context& context, const char& error) -> fpromise::result<int, char> {
                  ASSERT_CRITICAL(&context == &fake_context);
                  run_count++;
                  return fpromise::error(static_cast<char>(error + 1));
                });

    fpromise::result<int, char> result = p(fake_context);
    EXPECT_EQ(4, run_count);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('e', result.error());
    EXPECT_FALSE(p);
  }
}

TEST(PromiseTests, inspect_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  // Handler signature: void(const fpromise::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto p = make_delayed_ok_promise(42).inspect([&](const fpromise::result<int, char>& result) {
      ASSERT_CRITICAL(result.value() == 42);
      run_count++;
    });

    fpromise::result<int, char> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // Chaining on ERROR.
  // Handler signature: void(const fpromise::result<int, char>&).
  {
    uint64_t run_count = 0;
    auto p =
        make_delayed_error_promise('x').inspect([&](const fpromise::result<int, char>& result) {
          ASSERT_CRITICAL(result.error() == 'x');
          run_count++;
        });

    fpromise::result<int, char> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(0, run_count);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(1, run_count);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_EQ('x', result.error());
  }

  // Cover all handler argument signatures, more briefly.
  {
    uint64_t run_count = 0;
    auto p =
        make_checked_ok_promise(42)
            .inspect([&](fpromise::result<int, char>& result) {
              ASSERT_CRITICAL(result.value() == 42);
              run_count++;
              result = fpromise::ok(result.value() + 1);
            })
            .inspect([&](const fpromise::result<int, char>& result) {
              ASSERT_CRITICAL(result.value() == 43);
              run_count++;
            })
            .inspect([&](fpromise::context& context, fpromise::result<int, char>& result) {
              ASSERT_CRITICAL(result.value() == 43);
              ASSERT_CRITICAL(&context == &fake_context);
              run_count++;
              result = fpromise::ok(result.value() + 1);
            })
            .inspect([&](fpromise::context& context, const fpromise::result<int, char>& result) {
              ASSERT_CRITICAL(result.value() == 44);
              ASSERT_CRITICAL(&context == &fake_context);
              run_count++;
            });

    fpromise::result<int, char> result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(4, run_count);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(44, result.value());
  }
}

TEST(PromiseTests, discard_result_combinator) {
  fake_context fake_context;

  // Chaining on OK.
  {
    auto p = make_delayed_ok_promise(42).discard_result();
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");

    fpromise::result<> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
  }

  // Chaining on ERROR.
  {
    auto p = make_delayed_error_promise('x').discard_result();
    static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
    static_assert(std::is_same<void, decltype(p)::error_type>::value, "");

    fpromise::result<> result = p(fake_context);
    EXPECT_TRUE(p);
    EXPECT_EQ(fpromise::result_state::pending, result.state());

    result = p(fake_context);
    EXPECT_FALSE(p);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
  }
}

TEST(PromiseTests, wrap_with_combinator) {
  fake_context fake_context;
  capture_result_wrapper<int, char> wrapper;
  uint64_t successor_run_count = 0;

  // Apply a wrapper which steals a promise's result th
  auto p = make_delayed_ok_promise(42).wrap_with(wrapper).then(
      [&](const fpromise::result<>&) { successor_run_count++; });
  static_assert(std::is_same<void, decltype(p)::value_type>::value, "");
  static_assert(std::is_same<void, decltype(p)::error_type>::value, "");

  fpromise::result<> result = p(fake_context);
  EXPECT_TRUE(p);
  EXPECT_EQ(fpromise::result_state::pending, result.state());
  EXPECT_EQ(fpromise::result_state::pending, wrapper.last_result.state());
  EXPECT_EQ(0, successor_run_count);

  result = p(fake_context);
  EXPECT_FALSE(p);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_EQ(fpromise::result_state::ok, wrapper.last_result.state());
  EXPECT_EQ(42, wrapper.last_result.value());
  EXPECT_EQ(1, successor_run_count);
}

TEST(PromiseTests, box_combinator) {
  fake_context fake_context;

  auto p =
      fpromise::make_promise([&]() -> fpromise::result<int, char> { return fpromise::ok(42); });
  static_assert(!std::is_same<fpromise::promise<int, char>, decltype(p)>::value, "");

  auto q = p.box();
  static_assert(std::is_same<fpromise::promise<int, char>, decltype(q)>::value, "");
  EXPECT_TRUE(q);
  EXPECT_FALSE(p);

  fpromise::result<int, char> result = q(fake_context);
  EXPECT_FALSE(q);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_EQ(42, result.value());
}

TEST(PromiseTests, join_combinator) {
  fake_context fake_context;

  auto p = fpromise::join_promises(make_checked_ok_promise(42),
                                   make_checked_error_promise('x').or_else(
                                       [](const char& error) { return fpromise::error('y'); }),
                                   make_delayed_ok_promise(55));
  EXPECT_TRUE(p);

  fpromise::result<std::tuple<fpromise::result<int, char>, fpromise::result<int, char>,
                              fpromise::result<int, char>>>
      result = p(fake_context);
  EXPECT_TRUE(p);
  EXPECT_EQ(fpromise::result_state::pending, result.state());

  result = p(fake_context);
  EXPECT_FALSE(p);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_EQ(42, std::get<0>(result.value()).value());
  EXPECT_EQ('y', std::get<1>(result.value()).error());
  EXPECT_EQ(55, std::get<2>(result.value()).value());
}

TEST(PromiseTests, join_combinator_move_only_result) {
  fake_context fake_context;

  // Add 1 + 2 to get 3, using a join combinator with a "then" continuation
  // to demonstrate how to optionally return an error.
  auto p = fpromise::join_promises(make_move_only_promise(1), make_move_only_promise(2))
               .then([](fpromise::result<std::tuple<fpromise::result<std::unique_ptr<int>, char>,
                                                    fpromise::result<std::unique_ptr<int>, char>>>&
                            wrapped_result) -> fpromise::result<std::unique_ptr<int>, char> {
                 auto results = wrapped_result.take_value();
                 if (std::get<0>(results).is_error() || std::get<1>(results).is_error()) {
                   return fpromise::error('e');
                 } else {
                   int value = *std::get<0>(results).value() + *std::get<1>(results).value();
                   return fpromise::ok(std::make_unique<int>(value));
                 }
               });
  EXPECT_TRUE(p);
  fpromise::result<std::unique_ptr<int>, char> result = p(fake_context);
  EXPECT_FALSE(p);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_EQ(3, *result.value());
}

TEST(PromiseTests, join_vector_combinator) {
  fake_context fake_context;

  std::vector<fpromise::promise<int, char>> promises;
  promises.push_back(make_checked_ok_promise(42));
  promises.push_back(make_checked_error_promise('x').or_else(
      [](const char& error) { return fpromise::error('y'); }));
  promises.push_back(make_delayed_ok_promise(55));
  auto p = fpromise::join_promise_vector(std::move(promises));
  EXPECT_TRUE(p);

  fpromise::result<std::vector<fpromise::result<int, char>>> result = p(fake_context);
  EXPECT_TRUE(p);
  EXPECT_EQ(fpromise::result_state::pending, result.state());

  result = p(fake_context);
  EXPECT_FALSE(p);
  EXPECT_EQ(fpromise::result_state::ok, result.state());
  EXPECT_EQ(42, result.value()[0].value());
  EXPECT_EQ('y', result.value()[1].error());
  EXPECT_EQ(55, result.value()[2].value());
}

// Ensure that fpromise::promise is considered nullable so that a promise can be
// directly stored as the continuation of another promise without any
// additional wrappers, similar to fit::function.
static_assert(fit::is_nullable<fpromise::promise<>>::value, "");

// Test return type adapation performed by handler invokers.
// These tests verify that the necessary specializations can be produced
// in all cases for handlers with various signatures.
namespace handler_invoker_test {

// handler returning void...
static_assert(
    std::is_same<fpromise::result<>, fpromise::internal::result_handler_invoker<
                                         void (*)(fpromise::result<int, double>&),
                                         fpromise::result<int, double>>::result_type>::value,
    "");
static_assert(std::is_same<fpromise::result<void, double>,
                           fpromise::internal::value_handler_invoker<
                               void (*)(int&), fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(
    std::is_same<fpromise::result<int, void>,
                 fpromise::internal::error_handler_invoker<
                     void (*)(double&), fpromise::result<int, double>>::result_type>::value,
    "");

// handler returning fpromise::pending_result...
static_assert(std::is_same<fpromise::result<>,
                           fpromise::internal::result_handler_invoker<
                               fpromise::pending_result (*)(fpromise::result<int, double>&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<void, double>,
                           fpromise::internal::value_handler_invoker<
                               fpromise::pending_result (*)(int&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<int, void>,
                           fpromise::internal::error_handler_invoker<
                               fpromise::pending_result (*)(double&),
                               fpromise::result<int, double>>::result_type>::value,
              "");

// handler returning fpromise::ok_result...
static_assert(std::is_same<fpromise::result<unsigned, void>,
                           fpromise::internal::result_handler_invoker<
                               fpromise::ok_result<unsigned> (*)(fpromise::result<int, double>&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<unsigned, double>,
                           fpromise::internal::value_handler_invoker<
                               fpromise::ok_result<unsigned> (*)(int&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<int, void>,
                           fpromise::internal::error_handler_invoker<
                               fpromise::ok_result<int> (*)(double&),
                               fpromise::result<int, double>>::result_type>::value,
              "");

// handler returning fpromise::error_result...
static_assert(std::is_same<fpromise::result<void, float>,
                           fpromise::internal::result_handler_invoker<
                               fpromise::error_result<float> (*)(fpromise::result<int, double>&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<void, double>,
                           fpromise::internal::value_handler_invoker<
                               fpromise::error_result<double> (*)(int&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<int, float>,
                           fpromise::internal::error_handler_invoker<
                               fpromise::error_result<float> (*)(double&),
                               fpromise::result<int, double>>::result_type>::value,
              "");

// handler returning fpromise::result...
static_assert(
    std::is_same<fpromise::result<unsigned, float>,
                 fpromise::internal::result_handler_invoker<
                     fpromise::result<unsigned, float> (*)(fpromise::result<int, double>&),
                     fpromise::result<int, double>>::result_type>::value,
    "");
static_assert(std::is_same<fpromise::result<unsigned, float>,
                           fpromise::internal::value_handler_invoker<
                               fpromise::result<unsigned, float> (*)(int&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<unsigned, float>,
                           fpromise::internal::error_handler_invoker<
                               fpromise::result<unsigned, float> (*)(double&),
                               fpromise::result<int, double>>::result_type>::value,
              "");

// handler returning fpromise::promise...
static_assert(
    std::is_same<fpromise::result<unsigned, float>,
                 fpromise::internal::result_handler_invoker<
                     fpromise::promise<unsigned, float> (*)(fpromise::result<int, double>&),
                     fpromise::result<int, double>>::result_type>::value,
    "");
static_assert(std::is_same<fpromise::result<unsigned, double>,
                           fpromise::internal::value_handler_invoker<
                               fpromise::promise<unsigned, double> (*)(int&),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<int, float>,
                           fpromise::internal::error_handler_invoker<
                               fpromise::promise<int, float> (*)(double&),
                               fpromise::result<int, double>>::result_type>::value,
              "");

// handler returning lambda...
[[maybe_unused]] auto result_continuation_lambda =
    [](fpromise::result<int, double>&) -> fpromise::result<unsigned, float> {
  return fpromise::pending();
};
[[maybe_unused]] auto value_continuation_lambda = [](int&) -> fpromise::result<unsigned, double> {
  return fpromise::pending();
};
[[maybe_unused]] auto error_continuation_lambda = [](double&) -> fpromise::result<int, float> {
  return fpromise::pending();
};
static_assert(std::is_same<fpromise::result<unsigned, float>,
                           fpromise::internal::result_handler_invoker<
                               decltype(result_continuation_lambda),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<unsigned, double>,
                           fpromise::internal::value_handler_invoker<
                               decltype(value_continuation_lambda),
                               fpromise::result<int, double>>::result_type>::value,
              "");
static_assert(std::is_same<fpromise::result<int, float>,
                           fpromise::internal::error_handler_invoker<
                               decltype(error_continuation_lambda),
                               fpromise::result<int, double>>::result_type>::value,
              "");

}  // namespace handler_invoker_test

// Test predicate which is used interally to improve the quality of
// compilation errors when an invalid continuation type is encountered.
namespace is_continuation_test {

static_assert(fpromise::internal::is_continuation<
                  fit::function<fpromise::result<>(fpromise::context&)>>::value,
              "");
static_assert(!fpromise::internal::is_continuation<fit::function<void(fpromise::context&)>>::value,
              "");
static_assert(!fpromise::internal::is_continuation<fit::function<fpromise::result<>()>>::value, "");
static_assert(!fpromise::internal::is_continuation<void>::value, "");

[[maybe_unused]] auto continuation_lambda = [](fpromise::context&) -> fpromise::result<> {
  return fpromise::pending();
};
[[maybe_unused]] auto invalid_lambda = [] {};

static_assert(fpromise::internal::is_continuation<decltype(continuation_lambda)>::value, "");
static_assert(!fpromise::internal::is_continuation<decltype(invalid_lambda)>::value, "");

}  // namespace is_continuation_test
}  // namespace

// These are compile-time diagnostic tests.
// We expect the following tests to fail at compile time and produce helpful
// static assertions when enabled manually.
#if 0
void diagnose_handler_with_invalid_return_type() {
    // Doesn't work because result isn't fpromise::result<>, fpromise::ok_result<>,
    // fpromise::error_result<>, fpromise::pending_result, a continuation, or void.
    fpromise::make_promise([]() -> int { return 0; });
}
#endif
#if 0
void diagnose_handler_with_too_few_arguments() {
    // Expected between 1 and 2 arguments, got 0.
    fpromise::make_promise([] {})
        .then([]() {});
}
#endif
#if 0
void diagnose_handler_with_too_many_arguments() {
    // Expected between 1 and 2 arguments, got 3.
    fpromise::make_promise([] {})
        .then([](fpromise::context&, const fpromise::result<>&, int excess) {});
}
#endif
#if 0
void diagnose_handler_with_invalid_context_arg() {
    // When there are two argument, the first must be fpromise::context&.
    fpromise::make_promise([] {})
        .then([](const fpromise::result<>&, int excess) {});
}
#endif
#if 0
void diagnose_handler_with_invalid_result_arg() {
    // The result type must match that produced by the prior.
    fpromise::make_promise([] {})
        .then([](const fpromise::result<int>& result) {});
}
#endif
#if 0
void diagnose_handler_with_invalid_value_arg() {
    // The value type must match that produced by the prior.
    fpromise::make_promise([] { return fpromise::ok(3.2f); })
        .and_then([](const int& value) {});
}
#endif
#if 0
void diagnose_handler_with_invalid_error_arg() {
    // The error type must match that produced by the prior.
    fpromise::make_promise([] { return fpromise::error(3.2f); })
        .or_else([](const int& error) {});
}
#endif
