// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/future.h>

#include <zxtest/zxtest.h>

#include "test_utils.h"

namespace {

TEST(PendingTaskTests, non_empty_task) {
  fasync::testing::immediate_executor executor;
  fasync::context& context = executor.context();

  {
    uint64_t run_count = 0;
    fasync::pending_task task(fasync::make_future([&]() -> fasync::poll<> {
      if (++run_count == 3)
        return fasync::done();
      return fasync::pending();
    }));

    EXPECT_FALSE(task(context));
    EXPECT_EQ(1, run_count);

    EXPECT_FALSE(task(context));
    EXPECT_EQ(2, run_count);

    EXPECT_TRUE(task(context));
    EXPECT_EQ(3, run_count);
  }

  {
    uint64_t run_count = 0;
    fasync::pending_task task(fasync::make_future([&]() -> fasync::poll<int> {
      if (++run_count == 2)
        return fasync::done(0);
      return fasync::pending();
    }));

    fasync::pending_task task_move(std::move(task));
    fasync::pending_task task_movemove = std::move(task_move);

    fasync::future<> future = task_movemove.take_future();
    EXPECT_TRUE(future(context).is_pending());
    EXPECT_EQ(1, run_count);

    EXPECT_TRUE(future(context).is_ready());
    EXPECT_EQ(2, run_count);
  }
}

}  // namespace
