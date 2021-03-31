// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/synchronous-executor/executor.h>

#include <atomic>
#include <memory>
#include <thread>

#include <zxtest/zxtest.h>

namespace synchronous_executor {
TEST(SynchronousExecutorTests, OnlyRunRunnableTasks) {
  synchronous_executor executor;
  int run_count = 0;
  fit::suspended_task task_handle;

  executor.schedule_task(fit::make_promise([&run_count, &task_handle](fit::context& context) {
    run_count++;
    task_handle = context.suspend_task();
    return fit::pending();
  }));

  executor.run_until_idle();
  executor.run_until_idle();
  ASSERT_EQ(run_count, 1);
  task_handle.resume_task();
  executor.run_until_idle();
  ASSERT_EQ(run_count, 2);
}

TEST(SynchronousExecutorTests, SuspendResumeTest) {
  synchronous_executor executor;
  int run_count = 0;
  fit::suspended_task task_handle;

  executor.schedule_task(fit::make_promise([&run_count, &task_handle](fit::context& context) {
    run_count++;
    task_handle = context.suspend_task();
    return fit::pending();
  }));

  executor.run_until_idle();
  ASSERT_EQ(run_count, 1);
  task_handle.resume_task();
  executor.run_until_idle();
  ASSERT_EQ(run_count, 2);
}

TEST(SynchronousExecutorTests, ExecutorIsReentrantSafe) {
  synchronous_executor executor;
  int run_count = 0;
  bool reentered = false;
  fit::suspended_task task_handle;

  executor.schedule_task(
      fit::make_promise([&run_count, &executor, &reentered](fit::context& context) {
        run_count++;
        bool set_var = false;
        executor.schedule_task(fit::make_promise([&set_var]() { set_var = true; }));
        EXPECT_FALSE(set_var);
        executor.run_until_idle();
        reentered = set_var;
        return fit::ok();
      }));

  executor.run_until_idle();
  ASSERT_EQ(run_count, 1);
  ASSERT_TRUE(reentered);
}

TEST(SynchronousExecutorTests, ExecutorIsThreadSafe) {
  synchronous_executor executor;
  std::atomic_size_t run_count = 0;
  std::thread thread([&]() {
    for (size_t i = 0; i < 1000; i++) {
      executor.schedule_task(fit::make_promise([&run_count](fit::context& context) {
        run_count++;
        return fit::ok();
      }));
      executor.run_until_idle();
    }
  });

  for (size_t i = 0; i < 1000; i++) {
    executor.schedule_task(fit::make_promise([&run_count](fit::context& context) {
      run_count++;
      return fit::ok();
    }));
    executor.run_until_idle();
  }
  thread.join();

  ASSERT_EQ(run_count.load(), 2000);
}

TEST(SynchronousExecutorTests, AbandonedTasksGetProperlyCleanedUp) {
  synchronous_executor executor;
  int run_count = 0;
  fit::suspended_task task_handle;
  int cleanup_count = 0;
  class AutoCleanup {
   public:
    AutoCleanup(int* counter) : counter_(counter) {}
    ~AutoCleanup() { (*counter_)++; }

   private:
    int* counter_;
  };
  auto cleanup = std::make_unique<AutoCleanup>(&cleanup_count);
  executor.schedule_task(fit::make_promise(
      [&run_count, &task_handle, cleaner = std::move(cleanup)](fit::context& context) {
        run_count++;
        task_handle = context.suspend_task();
        return fit::pending();
      }));

  executor.run_until_idle();
  ASSERT_EQ(cleanup_count, 0);
  ASSERT_EQ(run_count, 1);
  task_handle.reset();
  ASSERT_EQ(run_count, 1);
  ASSERT_EQ(cleanup_count, 1);
}

}  // namespace synchronous_executor
