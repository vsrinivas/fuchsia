// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/scoped_task_runner.h"

#include <lib/async-testutils/test_loop.h>

#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(ScopedTaskRunner, DelegateToDispatcher) {
  async::TestLoop loop;

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };
  ScopedTaskRunner task_runner(loop.dispatcher());
  task_runner.PostTask(increment_call);
  task_runner.PostDelayedTask(increment_call, zx::sec(0));
  task_runner.PostTaskForTime(increment_call, zx::time(0));

  loop.RunUntilIdle();
  EXPECT_EQ(3u, called);
}

TEST(ScopedTaskRunner, CancelOnDeletion) {
  async::TestLoop loop;

  uint8_t called = 0;
  auto increment_call = [&called] { ++called; };

  {
    ScopedTaskRunner task_runner(loop.dispatcher());
    task_runner.PostTask(increment_call);
    task_runner.PostDelayedTask(increment_call, zx::sec(0));
    task_runner.PostTaskForTime(increment_call, zx::time(0));
  }

  loop.RunUntilIdle();
  EXPECT_EQ(0u, called);
}

}  // namespace
}  // namespace callback
