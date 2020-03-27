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

bool empty_task() {
  BEGIN_TEST;

  fake_context context;

  {
    fit::pending_task empty;
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.take_promise());
  }

  {
    fit::pending_task empty(fit::promise<>(nullptr));
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.take_promise());
  }

  {
    fit::pending_task empty(fit::promise<double, int>(nullptr));
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.take_promise());
  }

  END_TEST;
}

bool non_empty_task() {
  BEGIN_TEST;

  fake_context context;

  {
    uint64_t run_count = 0;
    fit::pending_task task(fit::make_promise([&]() -> fit::result<> {
      if (++run_count == 3)
        return fit::ok();
      return fit::pending();
    }));
    EXPECT_TRUE(task);

    EXPECT_FALSE(task(context));
    EXPECT_EQ(1, run_count);
    EXPECT_TRUE(task);

    EXPECT_FALSE(task(context));
    EXPECT_EQ(2, run_count);
    EXPECT_TRUE(task);

    EXPECT_TRUE(task(context));
    EXPECT_EQ(3, run_count);
    EXPECT_FALSE(task);
    EXPECT_FALSE(task.take_promise());
  }

  {
    uint64_t run_count = 0;
    fit::pending_task task(fit::make_promise([&]() -> fit::result<int> {
      if (++run_count == 2)
        return fit::ok(0);
      return fit::pending();
    }));
    EXPECT_TRUE(task);

    fit::pending_task task_move(std::move(task));
    EXPECT_TRUE(task_move);
    EXPECT_FALSE(task);

    fit::pending_task task_movemove;
    task_movemove = std::move(task_move);
    EXPECT_TRUE(task_movemove);
    EXPECT_FALSE(task_move);

    fit::promise<> promise = task_movemove.take_promise();
    EXPECT_TRUE(promise);
    EXPECT_EQ(fit::result_state::pending, promise(context).state());
    EXPECT_EQ(1, run_count);

    EXPECT_EQ(fit::result_state::ok, promise(context).state());
    EXPECT_EQ(2, run_count);
    EXPECT_FALSE(promise);
  }

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(pending_task_tests)
RUN_TEST(empty_task)
RUN_TEST(non_empty_task)
END_TEST_CASE(pending_task_tests)
