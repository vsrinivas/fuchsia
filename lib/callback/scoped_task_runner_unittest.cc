// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/callback/scoped_task_runner.h"

#include <lib/async/task.h>
#include <lib/async-testutils/async_stub.h>

#include "gtest/gtest.h"


namespace callback {
namespace {

class FakeTaskRunner : public fxl::TaskRunner {
 public:
  inline static fxl::RefPtr<FakeTaskRunner> Create() {
    return AdoptRef(new FakeTaskRunner());
  }

  void PostTask(fxl::Closure task) override {
    tasks.push_back(std::move(task));
  }

  void PostTaskForTime(fxl::Closure task,
                       fxl::TimePoint /*target_time*/) override {
    tasks.push_back(std::move(task));
  }

  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta /*delay*/) override {
    tasks.push_back(std::move(task));
  }

  bool RunsTasksOnCurrentThread() override {
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


inline void InvokeTaskHandler(async_t* async, async_task_t* task) {
  task->handler(async, task, ZX_OK);
}

class FakeDispatcher : public async::AsyncStub {
 public:
  zx_status_t PostTask(async_task_t* task) override {
    tasks.push_back(task);
    return ZX_OK;
  }
  std::vector<async_task_t*> tasks;
};

TEST(ScopedTaskRunner, DelegateToDispatcher) {
  FakeDispatcher async;

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };
  ScopedTaskRunner task_runner(&async);
  task_runner.PostTask(increment_call);
  task_runner.PostDelayedTask(increment_call, fxl::TimeDelta::FromSeconds(0));
  task_runner.PostTaskForTime(increment_call, fxl::TimePoint::Now());;

  EXPECT_EQ(3u, async.tasks.size());
  for (const auto& task : async.tasks) {
    InvokeTaskHandler(&async, task);
  }

  EXPECT_EQ(3u, called);
}

TEST(ScopedTaskRunner, CancelOnDeletionII) {
  FakeDispatcher async;

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };

  {
    ScopedTaskRunner task_runner(&async);
    task_runner.PostTask(increment_call);
    task_runner.PostDelayedTask(increment_call, fxl::TimeDelta::FromSeconds(0));
    task_runner.PostTaskForTime(increment_call, fxl::TimePoint::Now());
  }

  EXPECT_EQ(3u, async.tasks.size());
  for (const auto& task : async.tasks) {
    InvokeTaskHandler(&async, task);
  }

  EXPECT_EQ(0u, called);
}

}  // namespace
}  // namespace callback
