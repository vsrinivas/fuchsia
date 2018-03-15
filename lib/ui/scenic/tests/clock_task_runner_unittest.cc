// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/clock_task_runner.h"

#include <array>
#include "gtest/gtest.h"

namespace scenic {
namespace test {

TEST(ClockTaskRunner, BasicClockFunctionality) {
  const std::array<zx_time_t, 4> kInitialTimes = {0, 10, 100, 77777777};
  std::array<zx_time_t, 7> kTickIntervals = {1, 1, 10, 10, 0, 10, 0};

  for (zx_time_t initial_time : kInitialTimes) {
    auto clock = ClockTaskRunner::New(initial_time);
    EXPECT_EQ(clock->GetNanos(), initial_time);

    zx_time_t current_time = initial_time;
    for (zx_time_t tick_interval : kTickIntervals) {
      current_time += tick_interval;
      clock->Tick(tick_interval);
      EXPECT_EQ(clock->GetNanos(), current_time);
    }
  }
}

// Tasks should be executed in the order that PostTask() is called.
TEST(ClockTaskRunner, PostTaskOrdering) {
  auto clock = ClockTaskRunner::New(0);

  fxl::TaskRunner* task_runner = clock.get();
  std::vector<std::string> task_output;

  task_runner->PostTask([task_runner, &task_output] {
    task_output.push_back("Task 1");
    task_runner->PostTask([task_runner, &task_output] {
      task_output.push_back("Task 1a");
      task_runner->PostTask(
          [&task_output] { task_output.push_back("Task 1b"); });
    });
    task_runner->PostTask([task_runner, &task_output] {
      task_output.push_back("Task 1c");
      task_runner->PostTask(
          [&task_output] { task_output.push_back("Task 1d"); });
    });
  });

  task_runner->PostTask([task_runner, &task_output] {
    task_output.push_back("Task 2");
    task_runner->PostTask([task_runner, &task_output] {
      task_output.push_back("Task 2a");
      task_runner->PostTask(
          [&task_output] { task_output.push_back("Task 2b"); });
    });
  });

  EXPECT_TRUE(task_output.empty());
  clock->Tick(0);
  ASSERT_EQ(task_output.size(), 8U);
  EXPECT_EQ(task_output[0], "Task 1");
  EXPECT_EQ(task_output[1], "Task 2");
  EXPECT_EQ(task_output[2], "Task 1a");
  EXPECT_EQ(task_output[3], "Task 1c");
  EXPECT_EQ(task_output[4], "Task 2a");
  EXPECT_EQ(task_output[5], "Task 1b");
  EXPECT_EQ(task_output[6], "Task 1d");
  EXPECT_EQ(task_output[7], "Task 2b");
}

// Tasks scheduled for the same time should execute in the order that they are
// posted, regardless of whether the time is specified by a TimeDelta or a
// TimePoint.
TEST(ClockTaskRunner, EquivalenceOfDeadlineAndDelay) {
  constexpr zx_time_t kInitialNanos = 1111;
  constexpr zx_time_t kDelayNanos = 666;
  constexpr zx_time_t kTargetNanos = kInitialNanos + kDelayNanos;

  auto clock = ClockTaskRunner::New(kInitialNanos);
  fxl::TaskRunner* task_runner = clock.get();
  std::vector<std::string> task_output;

  const auto kTimeDelta = fxl::TimeDelta::FromNanoseconds(kDelayNanos);
  const auto kTimePoint = fxl::TimePoint::FromEpochDelta(
      fxl::TimeDelta::FromNanoseconds(kTargetNanos));

  task_runner->PostTaskForTime(
      [task_runner, &task_output] { task_output.push_back("Task 1"); },
      kTimePoint);
  task_runner->PostDelayedTask(
      [task_runner, &task_output] { task_output.push_back("Task 2"); },
      kTimeDelta);
  task_runner->PostTaskForTime(
      [task_runner, &task_output] { task_output.push_back("Task 3"); },
      kTimePoint);
  task_runner->PostDelayedTask(
      [task_runner, &task_output] { task_output.push_back("Task 4"); },
      kTimeDelta);

  clock->Tick(kDelayNanos);
  ASSERT_EQ(task_output.size(), 4U);
  EXPECT_EQ(task_output[0], "Task 1");
  EXPECT_EQ(task_output[1], "Task 2");
  EXPECT_EQ(task_output[2], "Task 3");
  EXPECT_EQ(task_output[3], "Task 4");
}

// When a task runs, the time obtained from the clock should match the scheduled
// execution time.
TEST(ClockTaskRunner, ExecutionTime) {
  const std::array<zx_time_t, 4> kInitialTimes = {0, 10, 100, 77777777};

  for (zx_time_t initial_time : kInitialTimes) {
    auto clock_task_runner = ClockTaskRunner::New(initial_time);
    fxl::TaskRunner* task_runner = clock_task_runner.get();
    Clock* clock = clock_task_runner.get();

    std::vector<std::string> task_output;
    std::vector<zx_time_t> task_times;

    constexpr zx_time_t kDelayNanos1 = 123;
    constexpr zx_time_t kDelayNanos2 = 234;
    constexpr zx_time_t kDelayNanos3 = 345;
    constexpr zx_time_t kDelayNanos4 = 456;
    const auto kTimeDelta1 = fxl::TimeDelta::FromNanoseconds(kDelayNanos1);
    const auto kTimeDelta2 = fxl::TimeDelta::FromNanoseconds(kDelayNanos2);
    const auto kTimeDelta3 = fxl::TimeDelta::FromNanoseconds(kDelayNanos3);
    const auto kTimeDelta4 = fxl::TimeDelta::FromNanoseconds(kDelayNanos4);

    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 1");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta1);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 2");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta2);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 3");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta3);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 4");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta4);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 5");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta1);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 6");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta2);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 7");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta3);
    task_runner->PostDelayedTask(
        [clock, &task_output, &task_times] {
          task_output.push_back("Task 8");
          task_times.push_back(clock->GetNanos());
        },
        kTimeDelta4);

    // Tick the clock so that only two tasks run; the rest are delayed.
    clock_task_runner->Tick(kDelayNanos1);
    ASSERT_EQ(task_output.size(), 2U);
    ASSERT_EQ(task_times.size(), 2U);
    EXPECT_EQ(task_output[0], "Task 1");
    EXPECT_EQ(task_times[0], initial_time + kDelayNanos1);
    EXPECT_EQ(task_output[1], "Task 5");
    EXPECT_EQ(task_times[1], initial_time + kDelayNanos1);

    // Tick the clock so that the rest of the tasks run.
    clock_task_runner->Tick(kDelayNanos4);
    ASSERT_EQ(task_output.size(), 8U);
    ASSERT_EQ(task_times.size(), 8U);
    EXPECT_EQ(task_output[2], "Task 2");
    EXPECT_EQ(task_times[2], initial_time + kDelayNanos2);
    EXPECT_EQ(task_output[3], "Task 6");
    EXPECT_EQ(task_times[3], initial_time + kDelayNanos2);
    EXPECT_EQ(task_output[4], "Task 3");
    EXPECT_EQ(task_times[4], initial_time + kDelayNanos3);
    EXPECT_EQ(task_output[5], "Task 7");
    EXPECT_EQ(task_times[5], initial_time + kDelayNanos3);
    EXPECT_EQ(task_output[6], "Task 4");
    EXPECT_EQ(task_times[6], initial_time + kDelayNanos4);
    EXPECT_EQ(task_output[7], "Task 8");
    EXPECT_EQ(task_times[7], initial_time + kDelayNanos4);

    // The clock's time should be set to the
    EXPECT_EQ(clock->GetNanos(), initial_time + kDelayNanos1 + kDelayNanos4);
  }
}

}  // namespace test
}  // namespace scenic
