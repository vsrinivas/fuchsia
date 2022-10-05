// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/single_threaded_executor.h>
#include <lib/fit/defer.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "lib/fasync/future.h"

namespace {

TEST(SingleThreadedExecutorTests, running_tasks) {
  fasync::single_threaded_executor executor;
  uint64_t run_count[3] = {};

  // Schedule a task that runs once and increments a counter.
  executor.schedule(fasync::make_future([&] { run_count[0]++; }));
  EXPECT_EQ(0, run_count[0]);

  // Schedule a task that runs once, increments a counter, and scheduled another task.
  executor.schedule(fasync::make_future([&](fasync::context& context) {
    run_count[1]++;
    EXPECT_EQ(&context.executor(), &executor);
    context.executor().schedule(fasync::make_future([&] { run_count[2]++; }));
  }));
  EXPECT_EQ(0, run_count[0]);
  EXPECT_EQ(0, run_count[1]);
  EXPECT_EQ(0, run_count[2]);

  // We expect that all of the tasks will run to completion including newly scheduled tasks.
  executor.run();
  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, run_count[2]);
}

TEST(SingleThreadedExecutorTests, suspending_and_resuming_tasks) {
  fasync::single_threaded_executor executor;
  uint64_t run_count[5] = {};
  uint64_t resume_count[5] = {};

  // Schedule a task that suspends itself and immediately resumes.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[0] == 100) {
      return fasync::done();
    }
    resume_count[0]++;
    context.suspend_task().resume();
    return fasync::pending();
  }));
  EXPECT_EQ(0, run_count[0]);
  EXPECT_EQ(0, resume_count[0]);

  // Schedule a task that requires several iterations to complete, each time scheduling another task
  // to resume itself after suspension.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[1] == 100) {
      return fasync::done();
    }
    context.executor().schedule(fasync::make_future([&, s = context.suspend_task()]() mutable {
      resume_count[1]++;
      s.resume();
    }));
    return fasync::pending();
  }));
  EXPECT_EQ(0, run_count[1]);
  EXPECT_EQ(0, resume_count[1]);

  // Same as the above but use another thread to resume.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[2] == 100) {
      return fasync::done();
    }
    std::thread([&, s = context.suspend_task()]() mutable {
      resume_count[2]++;
      s.resume();
    }).detach();
    return fasync::pending();
  }));
  EXPECT_EQ(0, run_count[2]);
  EXPECT_EQ(0, resume_count[2]);

  // Schedule a task that suspends itself but doesn't actually return pending so it only runs once.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    run_count[3]++;
    context.suspend_task();
    return fasync::done();
  }));
  EXPECT_EQ(0, run_count[3]);
  EXPECT_EQ(0, resume_count[3]);

  // Schedule a task that suspends itself and arranges to be resumed on one of two other threads,
  // whichever gets there first.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[4] == 100) {
      return fasync::done();
    }

    // Race two threads to resume the task. Either can win. This is safe because these threads don't
    // capture references to local variables that might go out of scope when the test exits.
    std::thread([s = context.suspend_task()]() mutable { s.resume(); }).detach();
    std::thread([s = context.suspend_task()]() mutable { s.resume(); }).detach();
    return fasync::pending();
  }));
  EXPECT_EQ(0, run_count[4]);
  EXPECT_EQ(0, resume_count[4]);

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
  EXPECT_EQ(0, resume_count[4]);
}

TEST(SingleThreadedExecutorTests, abandoning_tasks) {
  uint64_t run_count[4] = {};
  uint64_t destruction[4] = {};
  {
    fasync::single_threaded_executor executor;

    // Schedule a task that returns pending without suspending itself so it is immediately
    // abandoned.
    executor.schedule(
        fasync::make_future([&, d = fit::defer([&] { destruction[0]++; })]() -> fasync::poll<> {
          run_count[0]++;
          return fasync::pending();
        }));

    // Schedule a task that suspends itself but drops the |suspended_task| object before returning
    // so it is immediately abandoned.
    executor.schedule(fasync::make_future(
        [&, d = fit::defer([&] { destruction[1]++; })](fasync::context& context) -> fasync::poll<> {
          run_count[1]++;
          context.suspend_task();  // ignore result
          return fasync::pending();
        }));

    // Schedule a task that suspends itself and drops the |suspended_task| object from a different
    // thread so it is abandoned concurrently.
    executor.schedule(fasync::make_future(
        [&, d = fit::defer([&] { destruction[2]++; })](fasync::context& context) -> fasync::poll<> {
          run_count[2]++;
          std::thread([s = context.suspend_task()] {}).detach();
          return fasync::pending();
        }));

    // Schedule a task that creates several suspended task handles and drops them all on the floor.
    executor.schedule(fasync::make_future(
        [&, d = fit::defer([&] { destruction[3]++; })](fasync::context& context) -> fasync::poll<> {
          run_count[3]++;
          fasync::suspended_task s[3];
          for (size_t i = 0; i < 3; i++) {
            s[i] = context.suspend_task();
          }
          return fasync::pending();
        }));

    // We expect the tasks to have been executed but to have been abandoned.
    executor.run();
  }

  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(1, destruction[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, destruction[1]);
  EXPECT_EQ(1, run_count[2]);
  EXPECT_EQ(1, destruction[2]);
  EXPECT_EQ(1, run_count[3]);
  EXPECT_EQ(1, destruction[3]);
}

TEST(SingleThreadedExecutorTests, block) {
  uint64_t run_count = 0;
  fit::result<fit::failed, int> result = (fasync::make_future([&] {
                                            run_count++;
                                            return fit::ok(42);
                                          }) |
                                          fasync::block)
                                             .value();
  EXPECT_EQ(42, result.value());
  EXPECT_EQ(1, run_count);
}

TEST(SingleThreadedExecutorTests, block_move_only_result) {
  constexpr int kGolden = 5;
  size_t run_count = 0;

  auto future = fasync::make_future([&] {
    run_count++;
    return fit::ok(std::make_unique<int>(kGolden));
  });

  fit::result<fit::failed, std::unique_ptr<int>> result = fasync::block(std::move(future)).value();
  EXPECT_EQ(kGolden, *result.value());
  EXPECT_EQ(1, run_count);
}

}  // namespace
