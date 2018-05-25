// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/tasks/one_shot_timer.h"

#include <queue>
#include <utility>

#include "gtest/gtest.h"

namespace fxl {
namespace {

class FakeTaskRunner : public TaskRunner {
 public:
  bool has_tasks() const { return !tasks_.empty(); }
  TimeDelta last_delay() const { return last_delay_; }

  void PostTask(Closure task) override {}

  void PostTaskForTime(Closure task, TimePoint target_time) override {}

  void PostDelayedTask(Closure task, TimeDelta delay) override {
    tasks_.push(task);
    last_delay_ = delay;
  }

  bool RunsTasksOnCurrentThread() override { return true; }

  void RunOneTask() {
    ASSERT_TRUE(has_tasks());
    Closure task = tasks_.front();
    tasks_.pop();
    task();
  }

 private:
  std::queue<Closure> tasks_;
  TimeDelta last_delay_;
};

TEST(OneShotTimerTest, Basic) {
  auto task_runner = fxl::MakeRefCounted<FakeTaskRunner>();
  OneShotTimer timer;
  bool did_run = false;

  EXPECT_FALSE(timer.is_started());

  timer.Start(task_runner.get(), [&did_run] { did_run = true; },
              TimeDelta::FromMilliseconds(10));
  EXPECT_TRUE(timer.is_started());
  EXPECT_TRUE(task_runner->has_tasks());
  EXPECT_EQ(TimeDelta::FromMilliseconds(10), task_runner->last_delay());

  task_runner->RunOneTask();
  EXPECT_TRUE(did_run);
  EXPECT_FALSE(timer.is_started());
  EXPECT_FALSE(task_runner->has_tasks());
}

TEST(OneShotTimerTest, StartAndStop) {
  auto task_runner = fxl::MakeRefCounted<FakeTaskRunner>();
  OneShotTimer timer;
  bool did_run = false;

  EXPECT_FALSE(timer.is_started());

  timer.Start(task_runner.get(), [&did_run] { did_run = true; },
              TimeDelta::FromMilliseconds(10));
  EXPECT_TRUE(timer.is_started());
  EXPECT_TRUE(task_runner->has_tasks());
  EXPECT_EQ(TimeDelta::FromMilliseconds(10), task_runner->last_delay());

  timer.Stop();
  EXPECT_FALSE(timer.is_started());

  task_runner->RunOneTask();
  EXPECT_FALSE(did_run);
  EXPECT_FALSE(task_runner->has_tasks());
}

TEST(OneShotTimerTest, StartAndRestart) {
  auto task_runner = fxl::MakeRefCounted<FakeTaskRunner>();
  OneShotTimer timer;
  int task_id = 0;

  EXPECT_FALSE(timer.is_started());

  timer.Start(task_runner.get(), [&task_id] { task_id = 1; },
              TimeDelta::FromMilliseconds(10));
  EXPECT_TRUE(timer.is_started());
  EXPECT_TRUE(task_runner->has_tasks());
  EXPECT_EQ(TimeDelta::FromMilliseconds(10), task_runner->last_delay());

  timer.Start(task_runner.get(), [&task_id] { task_id = 2; },
              TimeDelta::FromMilliseconds(20));
  EXPECT_TRUE(timer.is_started());
  EXPECT_TRUE(task_runner->has_tasks());
  EXPECT_EQ(TimeDelta::FromMilliseconds(20), task_runner->last_delay());

  task_runner->RunOneTask();
  EXPECT_EQ(0, task_id);
  EXPECT_TRUE(timer.is_started());
  EXPECT_TRUE(task_runner->has_tasks());

  task_runner->RunOneTask();
  EXPECT_EQ(2, task_id);
  EXPECT_FALSE(timer.is_started());
  EXPECT_FALSE(task_runner->has_tasks());
}

TEST(OneShotTimerTest, StartAndDestroy) {
  auto task_runner = fxl::MakeRefCounted<FakeTaskRunner>();
  bool did_run = false;

  {
    OneShotTimer timer;

    EXPECT_FALSE(timer.is_started());

    timer.Start(task_runner.get(), [&did_run] { did_run = true; },
                TimeDelta::FromMilliseconds(10));
    EXPECT_TRUE(timer.is_started());
    EXPECT_TRUE(task_runner->has_tasks());
    EXPECT_EQ(TimeDelta::FromMilliseconds(10), task_runner->last_delay());
  }

  task_runner->RunOneTask();
  EXPECT_FALSE(did_run);
  EXPECT_FALSE(task_runner->has_tasks());
}

}  // namespace
}  // namespace fxl
