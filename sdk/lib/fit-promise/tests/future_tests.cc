// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/promise.h>

#include <zxtest/zxtest.h>

#include "unittest_utils.h"

namespace {

class fake_context : public fpromise::context {
 public:
  fpromise::executor* executor() const override { ASSERT_CRITICAL(false); }
  fpromise::suspended_task suspend_task() override { ASSERT_CRITICAL(false); }
};

TEST(FutureTests, empty_future) {
  fake_context context;

  {
    fpromise::future<> nihil;
    EXPECT_EQ(fpromise::future_state::empty, nihil.state());
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
    fpromise::future<> nihil(nullptr);
    EXPECT_EQ(fpromise::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));
  }

  {
    fpromise::future<> nihil(fpromise::promise<>(nullptr));
    EXPECT_EQ(fpromise::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));
  }

  {
    fpromise::future<> nihil(fpromise::pending());
    EXPECT_EQ(fpromise::future_state::empty, nihil.state());
    EXPECT_FALSE(nihil);
    EXPECT_TRUE(nihil.is_empty());
    EXPECT_FALSE(nihil.is_pending());
    EXPECT_FALSE(nihil.is_ok());
    EXPECT_FALSE(nihil.is_error());
    EXPECT_FALSE(nihil.is_ready());
    EXPECT_FALSE(nihil(context));
  }
}

TEST(FutureTests, pending_future) {
  fake_context context;

  uint64_t run_count = 0;
  fpromise::future<int, int> fut(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<int, int> {
        if (++run_count == 3)
          return fpromise::ok(42);
        return fpromise::pending();
      }));
  EXPECT_EQ(fpromise::future_state::pending, fut.state());
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
  EXPECT_EQ(fpromise::future_state::ok, fut.state());
  EXPECT_EQ(fpromise::result_state::ok, fut.result().state());
  EXPECT_EQ(42, fut.result().value());

  // do something similar but this time produce an error to ensure
  // that this state change works as expected too
  fut = fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<int, int> {
    if (++run_count == 5)
      return fpromise::error(42);
    return fpromise::pending();
  });
  EXPECT_EQ(fpromise::future_state::pending, fut.state());
  EXPECT_FALSE(fut(context));
  EXPECT_EQ(4, run_count);
  EXPECT_TRUE(fut(context));
  EXPECT_EQ(5, run_count);
  EXPECT_EQ(fpromise::future_state::error, fut.state());
  EXPECT_EQ(fpromise::result_state::error, fut.result().state());
  EXPECT_EQ(42, fut.result().error());
}

TEST(FutureTests, ok_future) {
  fake_context context;
  fpromise::future<int> fut(fpromise::ok(42));
  EXPECT_EQ(fpromise::future_state::ok, fut.state());
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
  EXPECT_EQ(fpromise::result_state::ok, fut.result().state());
  EXPECT_EQ(42, fut.result().value());
  EXPECT_EQ(42, fut.value());

  // destructive access
  fut = fpromise::ok(43);
  EXPECT_EQ(fpromise::future_state::ok, fut.state());
  EXPECT_EQ(43, fut.take_result().value());
  EXPECT_EQ(fpromise::future_state::empty, fut.state());

  fut = fpromise::ok(44);
  EXPECT_EQ(fpromise::future_state::ok, fut.state());
  EXPECT_EQ(44, fut.take_value());
  EXPECT_EQ(fpromise::future_state::empty, fut.state());

  fut = fpromise::ok(45);
  EXPECT_EQ(fpromise::future_state::ok, fut.state());
  EXPECT_EQ(45, fut.take_ok_result().value);
  EXPECT_EQ(fpromise::future_state::empty, fut.state());
}

TEST(FutureTests, error_future) {
  fake_context context;
  fpromise::future<void, int> fut(fpromise::error(42));
  EXPECT_EQ(fpromise::future_state::error, fut.state());
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
  EXPECT_EQ(fpromise::result_state::error, fut.result().state());
  EXPECT_EQ(42, fut.result().error());
  EXPECT_EQ(42, fut.error());

  // destructive access
  fut = fpromise::error(43);
  EXPECT_EQ(fpromise::future_state::error, fut.state());
  EXPECT_EQ(43, fut.take_result().error());
  EXPECT_EQ(fpromise::future_state::empty, fut.state());

  fut = fpromise::error(44);
  EXPECT_EQ(fpromise::future_state::error, fut.state());
  EXPECT_EQ(44, fut.take_error());
  EXPECT_EQ(fpromise::future_state::empty, fut.state());

  fut = fpromise::error(45);
  EXPECT_EQ(fpromise::future_state::error, fut.state());
  EXPECT_EQ(45, fut.take_error_result().error);
  EXPECT_EQ(fpromise::future_state::empty, fut.state());
}

TEST(FutureTests, assignment_and_swap) {
  fpromise::future<> x;
  EXPECT_EQ(fpromise::future_state::empty, x.state());

  x = fpromise::ok();
  EXPECT_EQ(fpromise::future_state::ok, x.state());

  x = fpromise::error();
  EXPECT_EQ(fpromise::future_state::error, x.state());

  x = fpromise::pending();
  EXPECT_EQ(fpromise::future_state::empty, x.state());

  x = nullptr;
  EXPECT_EQ(fpromise::future_state::empty, x.state());

  x = fpromise::promise<>();
  EXPECT_EQ(fpromise::future_state::empty, x.state());

  x = fpromise::make_promise([] {});
  EXPECT_EQ(fpromise::future_state::pending, x.state());

  fpromise::future<> y(std::move(x));
  EXPECT_EQ(fpromise::future_state::pending, y.state());
  EXPECT_EQ(fpromise::future_state::empty, x.state());

  x.swap(y);
  EXPECT_EQ(fpromise::future_state::pending, x.state());
  EXPECT_EQ(fpromise::future_state::empty, y.state());

  x.swap(x);
  EXPECT_EQ(fpromise::future_state::pending, x.state());
}

TEST(FutureTests, make_future) {
  fake_context context;
  uint64_t run_count = 0;
  auto fut = fpromise::make_future(fpromise::make_promise([&] {
    run_count++;
    return fpromise::ok(42);
  }));
  EXPECT_TRUE(fut(context));
  EXPECT_EQ(42, fut.value());
}

// Ensure that fpromise::future is considered nullable so that there is
// consistency with the fact that it can be initialized and assigned from
// nullptr similar to fit::function.
static_assert(fit::is_nullable<fpromise::future<>>::value, "");

}  // namespace
