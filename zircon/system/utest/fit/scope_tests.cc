// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <thread>

#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/fit/scope.h>
#include <lib/fit/single_threaded_executor.h>
#include <unittest/unittest.h>

#include "unittest_utils.h"

namespace {

class fake_context : public fit::context {
 public:
  fit::executor* executor() const override { ASSERT_CRITICAL(false); }
  fit::suspended_task suspend_task() override { ASSERT_CRITICAL(false); }
};

// Asynchronously accumulates a sum.
// This is an example of an object that offers promises that captures
// the "this" pointer, thereby needing a scope to prevent dangling pointers
// in case it is destroyed before the promises complete.
class accumulator {
 public:
  // Adds a value to the counter then returns it.
  // Takes time proportional to the value being added.
  fit::promise<uint32_t> add(uint32_t value) {
    return fit::make_promise(
               [this, cycles = value](fit::context& context) mutable -> fit::result<uint32_t> {
                 if (cycles == 0)
                   return fit::ok(counter_);
                 counter_++;
                 cycles--;
                 context.suspend_task().resume_task();
                 return fit::pending();
               })
        .wrap_with(scope_);
  }

  // Gets the current count, immediately.
  uint32_t count() const { return counter_; }

 private:
  fit::scope scope_;
  uint32_t counter_ = 0;
};

bool scoping_tasks() {
  BEGIN_TEST;

  auto acc = std::make_unique<accumulator>();
  fit::single_threaded_executor executor;
  uint32_t sums[4] = {};

  // Schedule some tasks which accumulate values asynchronously.
  executor.schedule_task(acc->add(2).and_then([&](const uint32_t& value) { sums[0] = value; }));
  executor.schedule_task(acc->add(1).and_then([&](const uint32_t& value) { sums[1] = value; }));
  executor.schedule_task(acc->add(5).and_then([&](const uint32_t& value) { sums[2] = value; }));

  // Schedule a task which accumulates and then destroys the accumulator
  // so that the scope is exited.  Any remaining promises will be aborted.
  uint32_t last_count = 0;
  executor.schedule_task(acc->add(3).and_then([&](const uint32_t& value) {
    sums[3] = value;
    // Schedule destruction in another task to avoid re-entrance.
    executor.schedule_task(fit::make_promise([&] {
      last_count = acc->count();
      acc.reset();
    }));
  }));

  // Run the tasks.
  executor.run();

  // The counts reflect the fact that the scope is exited part-way through
  // the cycle.  For example, the sums[2] task doesn't get to run since
  // it only runs after 5 cycles and the scope is exited on the third.
  EXPECT_EQ(11, last_count);
  EXPECT_EQ(7, sums[0]);
  EXPECT_EQ(5, sums[1]);
  EXPECT_EQ(0, sums[2]);
  EXPECT_EQ(10, sums[3]);

  END_TEST;
}

bool exit_destroys_wrapped_promises() {
  BEGIN_TEST;

  fit::scope scope;
  EXPECT_FALSE(scope.exited());

  // Set up three wrapped promises.
  bool destroyed[4] = {};
  auto p0 = scope.wrap(
      fit::make_promise([d = fit::defer([&] { destroyed[0] = true; })] { return fit::ok(); }));
  auto p1 = scope.wrap(
      fit::make_promise([d = fit::defer([&] { destroyed[1] = true; })] { return fit::ok(); }));
  auto p2 = scope.wrap(
      fit::make_promise([d = fit::defer([&] { destroyed[2] = true; })] { return fit::ok(); }));
  EXPECT_FALSE(destroyed[0]);
  EXPECT_FALSE(destroyed[1]);
  EXPECT_FALSE(destroyed[2]);

  // Execute one of them to completion, causing it to be destroyed.
  EXPECT_TRUE(fit::run_single_threaded(std::move(p1)).is_ok());
  EXPECT_FALSE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_FALSE(destroyed[2]);

  // Exit the scope, causing the wrapped promise to be destroyed
  // while still leaving the wrapper alive (but aborted).
  scope.exit();
  EXPECT_TRUE(scope.exited());
  EXPECT_TRUE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);

  // Wrapping another promise causes the wrapped promise to be immediately
  // destroyed.
  auto p3 = scope.wrap(
      fit::make_promise([d = fit::defer([&] { destroyed[3] = true; })] { return fit::ok(); }));
  EXPECT_TRUE(destroyed[3]);

  // Executing the wrapped promises returns pending.
  EXPECT_TRUE(fit::run_single_threaded(std::move(p0)).is_pending());
  EXPECT_TRUE(fit::run_single_threaded(std::move(p2)).is_pending());
  EXPECT_TRUE(fit::run_single_threaded(std::move(p3)).is_pending());

  // Exiting again has no effect.
  scope.exit();
  EXPECT_TRUE(scope.exited());

  END_TEST;
}

bool double_wrap() {
  BEGIN_TEST;

  fit::scope scope;
  fake_context context;

  // Here we wrap a task that's already been wrapped to see what happens
  // when the scope is exited.  This is interesting because it means that
  // the destruction of one wrapped promise will cause the destruction of
  // another wrapped promise and could uncover re-entrance issues.
  uint32_t run_count = 0;
  bool destroyed = false;
  auto promise =
      fit::make_promise([&, d = fit::defer([&] { destroyed = true; })](fit::context& context) {
        run_count++;
        return fit::pending();
      })
          .wrap_with(scope)
          .wrap_with(scope);  // wrap again!

  // Run the promise once to show that we can.
  EXPECT_EQ(fit::result_state::pending, promise(context).state());
  EXPECT_EQ(1, run_count);
  EXPECT_FALSE(destroyed);

  // Now exit the scope, which should cause the promise to be destroyed.
  scope.exit();
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(destroyed);

  // Running the promise again should do nothing.
  EXPECT_EQ(fit::result_state::pending, promise(context).state());
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(destroyed);

  END_TEST;
}

bool thread_safety() {
  BEGIN_TEST;

  fit::scope scope;
  fit::single_threaded_executor executor;
  uint64_t run_count = 0;

  // Schedule work from a few threads, just to show that we can.
  // Part way through, exit the scope.
  constexpr int num_threads = 4;
  constexpr int num_tasks_per_thread = 100;
  constexpr int exit_threshold = 75;
  std::thread threads[num_threads];
  for (int i = 0; i < num_threads; i++) {
    fit::bridge bridge;
    threads[i] = std::thread([&, completer = std::move(bridge.completer)]() mutable {
      for (int j = 0; j < num_tasks_per_thread; j++) {
        if (j == exit_threshold) {
          executor.schedule_task(fit::make_promise([&] { scope.exit(); }));
        }

        executor.schedule_task(fit::make_promise([&] { run_count++; }).wrap_with(scope));
      }
      completer.complete_ok();
    });
    executor.schedule_task(bridge.consumer.promise());
  }

  // Run the tasks.
  executor.run();
  for (int i = 0; i < num_threads; i++)
    threads[i].join();

  // We expect some non-deterministic number of tasks to have run
  // related to the exit threshold.
  // We scheduled num_threads * num_tasks_per_thread tasks, but on each thread
  // we exited the (common) scope after scheduling its first exit_threshold
  // tasks.  Once one of those threads exits the scope, no more tasks
  // (scheduled by any thread) will run within the scope, so the number of
  // executed tasks cannot increase any further.  Therefore we know that at
  // least exit_threshold tasks have run but we could have run as many as
  // num_threads * exit_threshold in a perfect world where all of the threads
  // called scope.exit() at the same time.
  EXPECT_GE(run_count, exit_threshold);
  EXPECT_LE(run_count, num_threads * exit_threshold);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(scope_tests)
RUN_TEST(scoping_tasks)
RUN_TEST(exit_destroys_wrapped_promises)
RUN_TEST(double_wrap)
RUN_TEST(thread_safety)
END_TEST_CASE(scope_tests)
