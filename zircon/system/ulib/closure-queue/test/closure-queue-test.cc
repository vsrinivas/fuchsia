// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

#include "lib/closure-queue/closure_queue.h"

namespace {

class ClosureQueueTest : public zxtest::Test {
 protected:
  ClosureQueueTest();
  async::Loop loop_;
  ClosureQueue queue_;
};

ClosureQueueTest::ClosureQueueTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread), queue_(loop_.dispatcher(), thrd_current()) {
  // nothing else to do here
}

TEST_F(ClosureQueueTest, ThrdTDefaultZero) {
  // The ClosureQueue implementation relies on this currently, so check here.
  EXPECT_FALSE(thrd_t{});
}

TEST_F(ClosureQueueTest, StopAndClearDoesNotRunMoreTasks) {
  bool closure_ran = false;
  queue_.Enqueue([&closure_ran] { closure_ran = true; });
  loop_.RunUntilIdle();
  EXPECT_TRUE(closure_ran);
  closure_ran = false;
  queue_.Enqueue([&closure_ran] { closure_ran = true; });
  queue_.StopAndClear();
  EXPECT_TRUE(queue_.is_stopped());
  loop_.RunUntilIdle();
  EXPECT_FALSE(closure_ran);
}

TEST_F(ClosureQueueTest, RunOneHere) {
  bool closure_ran = false;
  queue_.Enqueue([&closure_ran] { closure_ran = true; });
  queue_.RunOneHere();
  EXPECT_TRUE(closure_ran);
}

TEST_F(ClosureQueueTest, SetDispatcher) {
  ClosureQueue queue;
  queue.SetDispatcher(loop_.dispatcher(), thrd_current());
  bool closure_ran = false;
  queue.Enqueue([&closure_ran] { closure_ran = true; });
  loop_.RunUntilIdle();
  EXPECT_TRUE(closure_ran);
}

TEST_F(ClosureQueueTest, StopAndClearDuringTask) {
  bool task_1_ran = false;
  bool task_1_deleted = false;
  bool task_2_ran = false;
  bool task_2_deleted = false;
  auto curry_with_1 = fit::defer([&task_1_deleted] { task_1_deleted = true; });
  auto curry_with_2 = fit::defer([&task_2_deleted] { task_2_deleted = true; });
  queue_.Enqueue([this, &task_1_ran, curry_with_1 = std::move(curry_with_1)] {
    task_1_ran = true;
    queue_.StopAndClear();
  });
  queue_.Enqueue([&task_2_ran, curry_with_2 = std::move(curry_with_2)] { task_2_ran = true; });
  loop_.RunUntilIdle();
  EXPECT_TRUE(task_1_ran);
  EXPECT_TRUE(task_1_deleted);
  EXPECT_FALSE(task_2_ran);
  EXPECT_TRUE(task_2_deleted);
}

TEST_F(ClosureQueueTest, DispatcherThread) {
  // Constructed with a dispatcher already.
  EXPECT_TRUE(queue_.dispatcher_thread());
  EXPECT_EQ(queue_.dispatcher_thread(), thrd_current());

  // Default constructed then SetDispatcher().
  ClosureQueue queue;
  queue.SetDispatcher(loop_.dispatcher(), thrd_current());
  EXPECT_TRUE(queue.dispatcher_thread());
  EXPECT_EQ(queue.dispatcher_thread(), thrd_current());
}

}  // namespace
