// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cancelable_task.h"

#include "gtest/gtest.h"

#include "lib/fsl/tasks/message_loop.h"

namespace btlib {
namespace common {
namespace {

TEST(CancelableTaskTest, IsPosted) {
  fsl::MessageLoop loop;

  CancelableTask task;
  EXPECT_FALSE(task.posted());

  EXPECT_TRUE(task.Post([] {}, zx::duration(100)));
  EXPECT_TRUE(task.posted());

  task.Cancel();
  EXPECT_FALSE(task.posted());
}

// Tests that tasks can be posted more than once.
TEST(CancelableTaskTest, PostAgain) {
  constexpr auto kTimeout = zx::msec(20);

  fsl::MessageLoop loop;
  CancelableTask task;

  bool called = false;
  auto func = [&called, &loop] {
    called = true;
    loop.QuitNow();
  };
  EXPECT_TRUE(task.Post(func, kTimeout));
  EXPECT_TRUE(task.posted());

  // Cannot post again before the task runs.
  EXPECT_FALSE(task.Post(func, kTimeout));
  loop.Run();

  EXPECT_TRUE(called);
  EXPECT_FALSE(task.posted());

  // Can post now.
  EXPECT_TRUE(task.Post(func, kTimeout));
}

}  // namespace
}  // namespace common
}  // namespace btlib
