// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/fpromise/single_threaded_executor.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "unittest_utils.h"

namespace {

TEST(SingleThreadedExecutorTests, running_tasks) {
  fpromise::single_threaded_executor executor;
  uint64_t run_count[3] = {};

  // Schedule a task that runs once and increments a counter.
  executor.schedule_task(fpromise::make_promise([&] { run_count[0]++; }));

  // Schedule a task that runs once, increments a counter,
  // and scheduled another task.
  executor.schedule_task(fpromise::make_promise([&](fpromise::context& context) {
    run_count[1]++;
    ASSERT_CRITICAL(context.executor() == &executor);
    context.executor()->schedule_task(fpromise::make_promise([&] { run_count[2]++; }));
  }));
  EXPECT_EQ(0, run_count[0]);
  EXPECT_EQ(0, run_count[1]);
  EXPECT_EQ(0, run_count[2]);

  // We expect that all of the tasks will run to completion including newly
  // scheduled tasks.
  executor.run();
  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, run_count[2]);
}

TEST(SingleThreadedExecutorTests, suspending_and_resuming_tasks) {
  fpromise::single_threaded_executor executor;
  uint64_t run_count[5] = {};
  uint64_t resume_count[5] = {};

  // Schedule a task that suspends itself and immediately resumes.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[0] == 100)
          return fpromise::ok();
        resume_count[0]++;
        context.suspend_task().resume_task();
        return fpromise::pending();
      }));

  // Schedule a task that requires several iterations to complete, each
  // time scheduling another task to resume itself after suspension.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[1] == 100)
          return fpromise::ok();
        context.executor()->schedule_task(
            fpromise::make_promise([&, s = context.suspend_task()]() mutable {
              resume_count[1]++;
              s.resume_task();
            }));
        return fpromise::pending();
      }));

  // Same as the above but use another thread to resume.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[2] == 100)
          return fpromise::ok();
        std::thread([&, s = context.suspend_task()]() mutable {
          resume_count[2]++;
          s.resume_task();
        }).detach();
        return fpromise::pending();
      }));

  // Schedule a task that suspends itself but doesn't actually return pending
  // so it only runs once.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        run_count[3]++;
        context.suspend_task();
        return fpromise::ok();
      }));

  // Schedule a task that suspends itself and arranges to be resumed on
  // one of two other threads, whichever gets there first.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[4] == 100)
          return fpromise::ok();

        // Race two threads to resume the task.  Either can win.
        // This is safe because these threads don't capture references to
        // local variables that might go out of scope when the test exits.
        std::thread([s = context.suspend_task()]() mutable { s.resume_task(); }).detach();
        std::thread([s = context.suspend_task()]() mutable { s.resume_task(); }).detach();
        return fpromise::pending();
      }));

  // We expect the tasks to have been completed after being resumed several times.
  executor.run();
  EXPECT_EQ(100, run_count[0]);
  EXPECT_EQ(99, resume_count[0]);
  EXPECT_EQ(100, run_count[1]);
  EXPECT_EQ(99, resume_count[1]);
  EXPECT_EQ(100, run_count[2]);
  EXPECT_EQ(99, resume_count[2]);
  EXPECT_EQ(1, run_count[3]);
  EXPECT_EQ(0, resume_count[3]);
  EXPECT_EQ(100, run_count[4]);
}

// Test disabled due to flakiness.  See fxbug.dev/8378.
TEST(SingleThreadedExecutorTests, DISABLED_abandoning_tasks) {
  fpromise::single_threaded_executor executor;
  uint64_t run_count[4] = {};
  uint64_t destruction[4] = {};

  // Schedule a task that returns pending without suspending itself
  // so it is immediately abandoned.
  executor.schedule_task(fpromise::make_promise(
      [&, d = fit::defer([&] { destruction[0]++; })]() -> fpromise::result<> {
        run_count[0]++;
        return fpromise::pending();
      }));

  // Schedule a task that suspends itself but drops the |suspended_task|
  // object before returning so it is immediately abandoned.
  executor.schedule_task(
      fpromise::make_promise([&, d = fit::defer([&] { destruction[1]++; })](
                                 fpromise::context& context) -> fpromise::result<> {
        run_count[1]++;
        context.suspend_task();  // ignore result
        return fpromise::pending();
      }));

  // Schedule a task that suspends itself and drops the |suspended_task|
  // object from a different thread so it is abandoned concurrently.
  executor.schedule_task(
      fpromise::make_promise([&, d = fit::defer([&] { destruction[2]++; })](
                                 fpromise::context& context) -> fpromise::result<> {
        run_count[2]++;
        std::thread([s = context.suspend_task()] {}).detach();
        return fpromise::pending();
      }));

  // Schedule a task that creates several suspended task handles and drops
  // them all on the floor.
  executor.schedule_task(
      fpromise::make_promise([&, d = fit::defer([&] { destruction[3]++; })](
                                 fpromise::context& context) -> fpromise::result<> {
        run_count[3]++;
        fpromise::suspended_task s[3];
        for (size_t i = 0; i < 3; i++)
          s[i] = context.suspend_task();
        return fpromise::pending();
      }));

  // We expect the tasks to have been executed but to have been abandoned.
  executor.run();
  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(1, destruction[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, destruction[1]);
  EXPECT_EQ(1, run_count[2]);
  EXPECT_EQ(1, destruction[2]);
  EXPECT_EQ(1, run_count[3]);
  EXPECT_EQ(1, destruction[3]);
}

TEST(SingleThreadedExecutorTests, run_single_threaded) {
  uint64_t run_count = 0;
  fpromise::result<int> result = fpromise::run_single_threaded(fpromise::make_promise([&]() {
    run_count++;
    return fpromise::ok(42);
  }));
  EXPECT_EQ(42, result.value());
  EXPECT_EQ(1, run_count);
}

TEST(SingleThreadedExecutorTests, run_single_threaded_move_only_result) {
  const int kGolden = 5;
  size_t run_count = 0;

  auto promise = fpromise::make_promise([&]() {
    run_count++;
    return fpromise::ok(std::make_unique<int>(kGolden));
  });

  fpromise::result<std::unique_ptr<int>> result = fpromise::run_single_threaded(std::move(promise));
  EXPECT_EQ(kGolden, *result.value());
  EXPECT_EQ(1, run_count);
}

}  // namespace
