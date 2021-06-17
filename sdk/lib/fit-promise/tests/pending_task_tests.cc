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

TEST(PendingTaskTests, empty_task) {
  fake_context context;

  {
    fpromise::pending_task empty;
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.take_promise());
  }

  {
    fpromise::pending_task empty(fpromise::promise<>(nullptr));
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.take_promise());
  }

  {
    fpromise::pending_task empty(fpromise::promise<double, int>(nullptr));
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.take_promise());
  }
}

TEST(PendingTaskTests, non_empty_task) {
  fake_context context;

  {
    uint64_t run_count = 0;
    fpromise::pending_task task(fpromise::make_promise([&]() -> fpromise::result<> {
      if (++run_count == 3)
        return fpromise::ok();
      return fpromise::pending();
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
    fpromise::pending_task task(fpromise::make_promise([&]() -> fpromise::result<int> {
      if (++run_count == 2)
        return fpromise::ok(0);
      return fpromise::pending();
    }));
    EXPECT_TRUE(task);

    fpromise::pending_task task_move(std::move(task));
    EXPECT_TRUE(task_move);
    EXPECT_FALSE(task);

    fpromise::pending_task task_movemove;
    task_movemove = std::move(task_move);
    EXPECT_TRUE(task_movemove);
    EXPECT_FALSE(task_move);

    fpromise::promise<> promise = task_movemove.take_promise();
    EXPECT_TRUE(promise);
    EXPECT_EQ(fpromise::result_state::pending, promise(context).state());
    EXPECT_EQ(1, run_count);

    EXPECT_EQ(fpromise::result_state::ok, promise(context).state());
    EXPECT_EQ(2, run_count);
    EXPECT_FALSE(promise);
  }
}

}  // namespace
