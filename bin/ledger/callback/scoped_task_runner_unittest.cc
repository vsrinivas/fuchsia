// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/scoped_task_runner.h"

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"

namespace callback {
namespace {

class FakeTaskRunner : public fxl::TaskRunner {
 public:
  inline static fxl::RefPtr<FakeTaskRunner> Create() {
    return AdoptRef(new FakeTaskRunner());
  }

  void PostTask(fxl::Closure task) { tasks.push_back(std::move(task)); }

  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time) {
    tasks.push_back(std::move(task));
  }

  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) {
    tasks.push_back(std::move(task));
  }

  bool RunsTasksOnCurrentThread() {
    runs_task_on_current_thread_called = true;
    return true;
  }

  std::vector<fxl::Closure> tasks;
  bool runs_task_on_current_thread_called = false;
};

TEST(ScopedTaskRunner, DelegateToTaskRunner) {
  auto base_task_runner = FakeTaskRunner::Create();

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };
  ScopedTaskRunner task_runner(base_task_runner);
  task_runner.PostTask(increment_call);
  task_runner.PostDelayedTask(increment_call, fxl::TimeDelta::FromSeconds(0));
  task_runner.PostTaskForTime(increment_call, fxl::TimePoint::Now());

  EXPECT_TRUE(task_runner.RunsTasksOnCurrentThread());
  EXPECT_TRUE(base_task_runner->runs_task_on_current_thread_called);

  EXPECT_EQ(3u, base_task_runner->tasks.size());
  for (const auto& task : base_task_runner->tasks) {
    task();
  }

  EXPECT_EQ(3u, called);
}

TEST(ScopedTaskRunner, CancelOnDeletion) {
  auto base_task_runner = FakeTaskRunner::Create();

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };

  {
    ScopedTaskRunner task_runner(base_task_runner);
    task_runner.PostTask(increment_call);
    task_runner.PostDelayedTask(increment_call, fxl::TimeDelta::FromSeconds(0));
    task_runner.PostTaskForTime(increment_call, fxl::TimePoint::Now());
  }

  EXPECT_EQ(3u, base_task_runner->tasks.size());
  for (const auto& task : base_task_runner->tasks) {
    task();
  }

  EXPECT_EQ(0u, called);
}

}  // namespace
}  // namespace callback
