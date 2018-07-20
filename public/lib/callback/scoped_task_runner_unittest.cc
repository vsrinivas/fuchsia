// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/scoped_task_runner.h"

#include <lib/async-testutils/dispatcher_stub.h>
#include <lib/async/task.h>

#include "gtest/gtest.h"

namespace callback {
namespace {

inline void InvokeTaskHandler(async_dispatcher_t* dispatcher,
                              async_task_t* task) {
  task->handler(dispatcher, task, ZX_OK);
}

class FakeDispatcher : public async::DispatcherStub {
 public:
  zx_status_t PostTask(async_task_t* task) override {
    tasks.push_back(task);
    return ZX_OK;
  }
  std::vector<async_task_t*> tasks;
};

TEST(ScopedTaskRunner, DelegateToDispatcher) {
  FakeDispatcher dispatcher;

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };
  ScopedTaskRunner task_runner(&dispatcher);
  task_runner.PostTask(increment_call);
  task_runner.PostDelayedTask(increment_call, zx::sec(0));
  task_runner.PostTaskForTime(increment_call, zx::time(0));

  EXPECT_EQ(3u, dispatcher.tasks.size());
  for (const auto& task : dispatcher.tasks) {
    InvokeTaskHandler(&dispatcher, task);
  }

  EXPECT_EQ(3u, called);
}

TEST(ScopedTaskRunner, CancelOnDeletion) {
  FakeDispatcher dispatcher;

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };

  {
    ScopedTaskRunner task_runner(&dispatcher);
    task_runner.PostTask(increment_call);
    task_runner.PostDelayedTask(increment_call, zx::sec(0));
    task_runner.PostTaskForTime(increment_call, zx::time(0));
  }

  EXPECT_EQ(3u, dispatcher.tasks.size());
  for (const auto& task : dispatcher.tasks) {
    InvokeTaskHandler(&dispatcher, task);
  }

  EXPECT_EQ(0u, called);
}

}  // namespace
}  // namespace callback
