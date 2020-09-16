// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <condition_variable>
#include <mutex>

#include <fs/journal/background_executor.h>
#include <gtest/gtest.h>

namespace fs {
namespace {

TEST(BackgroundExecutorTest, Creation) { BackgroundExecutor executor; }

// Ensure we can destroy an executor with scheduled tasks.
TEST(BackgroundExecutorTest, DestructorCompletesOneScheduledTask) {
  bool called = false;
  {
    BackgroundExecutor executor;
    executor.schedule_task(fit::make_promise([&called]() -> fit::result<> {
      EXPECT_FALSE(called);
      called = true;
      return fit::ok();
    }));
  }
  ASSERT_TRUE(called);
}

// Ensure we can schedule many tasks.
TEST(BackgroundExecutorTest, DestructorCompletesManyScheduledTasks) {
  const size_t kTotalTasks = 10;
  size_t counter = 0;
  {
    BackgroundExecutor executor;
    for (size_t i = 0; i < kTotalTasks; i++) {
      executor.schedule_task(fit::make_promise([&counter]() -> fit::result<> {
        // Note: We don't bother comparing the order that these promises are scheduled,
        // since they may occur in any order.
        //
        // They are guaranteed to be executed by a single thread, so we increment this
        // counter non-atomically.
        counter++;
        return fit::ok();
      }));
    }
  }
  ASSERT_EQ(counter, kTotalTasks);
}

// Ensure we don't need to wait until the Executor terminates before the scheduled tasks execute.
TEST(BackgroundExecutorTest, ScheduleNotStalledUntilDestructor) {
  BackgroundExecutor executor;
  std::mutex mutex;
  std::condition_variable cvar;
  bool called = false;
  executor.schedule_task(fit::make_promise([&]() -> fit::result<> {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_FALSE(called);
    called = true;
    cvar.notify_one();
    return fit::ok();
  }));
  std::unique_lock<std::mutex> lock(mutex);
  cvar.wait(lock, [&called] { return called; });
  ASSERT_TRUE(called);
}

}  // namespace
}  // namespace fs
