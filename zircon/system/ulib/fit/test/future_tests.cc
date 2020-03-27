// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/promise.h>
#include <unittest/unittest.h>

#include "unittest_utils.h"

namespace {

class fake_context : public fit::context {
 public:
  fit::executor* executor() const override { ASSERT_CRITICAL(false); }
  fit::suspended_task suspend_task() override { ASSERT_CRITICAL(false); }
};

bool empty_future() {
  BEGIN_TEST;

  fake_context context;

  {
    fit::future<> nihil;
    EXPECT_EQ(fit::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));

    EXPECT_TRUE(nihil == nullptr);
    EXPECT_TRUE(nullptr == nihil);
    EXPECT_FALSE(nihil != nullptr);
    EXPECT_FALSE(nullptr != nihil);
  }

  {
    fit::future<> nihil(nullptr);
    EXPECT_EQ(fit::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));
  }

  {
    fit::future<> nihil(fit::promise<>(nullptr));
    EXPECT_EQ(fit::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));
  }

  {
    fit::future<> nihil(fit::pending());
    EXPECT_EQ(fit::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));
  }

  END_TEST;
}

bool pending_future() {
  BEGIN_TEST;

  fake_context context;

  uint64_t run_count = 0;
  fit::future<int, int> fut(fit::make_promise([&](fit::context& context) -> fit::result<int, int> {
    if (++run_count == 3)
      return fit::ok(42);
    return fit::pending();
  }));
  EXPECT_EQ(fit::future_state::pending, fut.state());
  EXPECT_TRUE(fut);
  EXPECT_FALSE(fut.is_empty());
  EXPECT_TRUE(fut.is_pending());
  EXPECT_FALSE(fut.is_ok());
  EXPECT_FALSE(fut.is_error());
  EXPECT_FALSE(fut.is_ready());

  EXPECT_FALSE(fut == nullptr);
  EXPECT_FALSE(nullptr == fut);
  EXPECT_TRUE(fut != nullptr);
  EXPECT_TRUE(nullptr != fut);

  // evaluate the future
  EXPECT_FALSE(fut(context));
  EXPECT_EQ(1, run_count);
  EXPECT_FALSE(fut(context));
  EXPECT_EQ(2, run_count);
  EXPECT_TRUE(fut(context));
  EXPECT_EQ(3, run_count);

  // check the result
  EXPECT_EQ(fit::future_state::ok, fut.state());
  EXPECT_EQ(fit::result_state::ok, fut.result().state());
  EXPECT_EQ(42, fut.result().value());

  // do something similar but this time produce an error to ensure
  // that this state change works as expected too
  fut = fit::make_promise([&](fit::context& context) -> fit::result<int, int> {
    if (++run_count == 5)
      return fit::error(42);
    return fit::pending();
  });
  EXPECT_EQ(fit::future_state::pending, fut.state());
  EXPECT_FALSE(fut(context));
  EXPECT_EQ(4, run_count);
  EXPECT_TRUE(fut(context));
  EXPECT_EQ(5, run_count);
  EXPECT_EQ(fit::future_state::error, fut.state());
  EXPECT_EQ(fit::result_state::error, fut.result().state());
  EXPECT_EQ(42, fut.result().error());

  END_TEST;
}

bool ok_future() {
  BEGIN_TEST;

  fake_context context;
  fit::future<int> fut(fit::ok(42));
  EXPECT_EQ(fit::future_state::ok, fut.state());
  EXPECT_TRUE(fut);
  EXPECT_FALSE(fut.is_empty());
  EXPECT_FALSE(fut.is_pending());
  EXPECT_TRUE(fut.is_ok());
  EXPECT_FALSE(fut.is_error());
  EXPECT_TRUE(fut.is_ready());
  EXPECT_TRUE(fut(context));

  EXPECT_FALSE(fut == nullptr);
  EXPECT_FALSE(nullptr == fut);
  EXPECT_TRUE(fut != nullptr);
  EXPECT_TRUE(nullptr != fut);

  // non-destructive access
  EXPECT_EQ(fit::result_state::ok, fut.result().state());
  EXPECT_EQ(42, fut.result().value());
  EXPECT_EQ(42, fut.value());

  // destructive access
  fut = fit::ok(43);
  EXPECT_EQ(fit::future_state::ok, fut.state());
  EXPECT_EQ(43, fut.take_result().value());
  EXPECT_EQ(fit::future_state::empty, fut.state());

  fut = fit::ok(44);
  EXPECT_EQ(fit::future_state::ok, fut.state());
  EXPECT_EQ(44, fut.take_value());
  EXPECT_EQ(fit::future_state::empty, fut.state());

  fut = fit::ok(45);
  EXPECT_EQ(fit::future_state::ok, fut.state());
  EXPECT_EQ(45, fut.take_ok_result().value);
  EXPECT_EQ(fit::future_state::empty, fut.state());

  END_TEST;
}

bool error_future() {
  BEGIN_TEST;

  fake_context context;
  fit::future<void, int> fut(fit::error(42));
  EXPECT_EQ(fit::future_state::error, fut.state());
  EXPECT_TRUE(fut);
  EXPECT_FALSE(fut.is_empty());
  EXPECT_FALSE(fut.is_pending());
  EXPECT_FALSE(fut.is_ok());
  EXPECT_TRUE(fut.is_error());
  EXPECT_TRUE(fut.is_ready());
  EXPECT_TRUE(fut(context));

  EXPECT_FALSE(fut == nullptr);
  EXPECT_FALSE(nullptr == fut);
  EXPECT_TRUE(fut != nullptr);
  EXPECT_TRUE(nullptr != fut);

  // non-destructive access
  EXPECT_EQ(fit::result_state::error, fut.result().state());
  EXPECT_EQ(42, fut.result().error());
  EXPECT_EQ(42, fut.error());

  // destructive access
  fut = fit::error(43);
  EXPECT_EQ(fit::future_state::error, fut.state());
  EXPECT_EQ(43, fut.take_result().error());
  EXPECT_EQ(fit::future_state::empty, fut.state());

  fut = fit::error(44);
  EXPECT_EQ(fit::future_state::error, fut.state());
  EXPECT_EQ(44, fut.take_error());
  EXPECT_EQ(fit::future_state::empty, fut.state());

  fut = fit::error(45);
  EXPECT_EQ(fit::future_state::error, fut.state());
  EXPECT_EQ(45, fut.take_error_result().error);
  EXPECT_EQ(fit::future_state::empty, fut.state());

  END_TEST;
}

bool assignment_and_swap() {
  BEGIN_TEST;

  fit::future<> x;
  EXPECT_EQ(fit::future_state::empty, x.state());

  x = fit::ok();
  EXPECT_EQ(fit::future_state::ok, x.state());

  x = fit::error();
  EXPECT_EQ(fit::future_state::error, x.state());

  x = fit::pending();
  EXPECT_EQ(fit::future_state::empty, x.state());

  x = nullptr;
  EXPECT_EQ(fit::future_state::empty, x.state());

  x = fit::promise<>();
  EXPECT_EQ(fit::future_state::empty, x.state());

  x = fit::make_promise([] {});
  EXPECT_EQ(fit::future_state::pending, x.state());

  fit::future<> y(std::move(x));
  EXPECT_EQ(fit::future_state::pending, y.state());
  EXPECT_EQ(fit::future_state::empty, x.state());

  x.swap(y);
  EXPECT_EQ(fit::future_state::pending, x.state());
  EXPECT_EQ(fit::future_state::empty, y.state());

  x.swap(x);
  EXPECT_EQ(fit::future_state::pending, x.state());

  END_TEST;
}

bool make_future() {
  BEGIN_TEST;

  fake_context context;
  uint64_t run_count = 0;
  auto fut = fit::make_future(fit::make_promise([&] {
    run_count++;
    return fit::ok(42);
  }));
  EXPECT_TRUE(fut(context));
  EXPECT_EQ(42, fut.value());

  END_TEST;
}

// Ensure that fit::future is considered nullable so that there is
// consistency with the fact that it can be initialized and assigned from
// nullptr similar to fit::function.
static_assert(fit::is_nullable<fit::future<>>::value, "");

}  // namespace

BEGIN_TEST_CASE(future_tests)
RUN_TEST(empty_future)
RUN_TEST(pending_future)
RUN_TEST(ok_future)
RUN_TEST(error_future)
RUN_TEST(assignment_and_swap)
RUN_TEST(make_future)
END_TEST_CASE(future_tests)
