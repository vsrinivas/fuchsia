// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/callback/scoped_task_runner.h>

#include <gtest/gtest.h>
#include <lib/async-testutils/test_loop.h>
#include <lib/fxl/macros.h>

namespace callback {
namespace {

constexpr zx::duration kInterval = zx::min(4);  // to save the world

struct MoveOnly {
  MoveOnly(MoveOnly&&) = default;
  bool called;
  FXL_DISALLOW_COPY_AND_ASSIGN(MoveOnly);
};

TEST(ScopedTaskRunnerTest, RunsTaskInScope) {
  bool called = false;
  ScopedTaskRunner tasks;
  tasks.MakeScoped([&] { called = true; })();
  EXPECT_TRUE(called);
}

TEST(ScopedTaskRunnerTest, TaskWithArg) {
  bool called = false;
  ScopedTaskRunner tasks;
  tasks.MakeScoped([&](bool value) { called = value; })(true);
  EXPECT_TRUE(called);
}

TEST(ScopedTaskRunnerTest, TaskWithRefArg) {
  bool called = false;
  ScopedTaskRunner tasks;
  tasks.MakeScoped([](bool& called) { called = true; })(called);
  EXPECT_TRUE(called);
}

TEST(ScopedTaskRunnerTest, TaskWithMoveOnlyArg) {
  bool called = false;
  ScopedTaskRunner tasks;
  tasks.MakeScoped([&](MoveOnly arg) { called = arg.called; })({true});
  EXPECT_TRUE(called);
}

TEST(ScopedTaskRunnerTest, TaskWithMoveOnlyCapture) {
  bool called = false;
  ScopedTaskRunner tasks;
  MoveOnly move_only{true};
  tasks.MakeScoped([&called, move_only = std::move(move_only)] {
    called = move_only.called;
  })();
  EXPECT_TRUE(called);
}

TEST(ScopedTaskRunnerTest, CancelsTaskOutOfScope) {
  bool called = false;
  fit::closure task;
  {
    ScopedTaskRunner tasks;
    task = tasks.MakeScoped([&] { called = true; });
  }
  task();
  EXPECT_FALSE(called);
}

TEST(ScopedTaskRunnerTest, ExplicitShutdown) {
  bool called = false;
  ScopedTaskRunner tasks;
  auto task = tasks.MakeScoped([&] { called = true; });
  tasks.ShutDown();
  task();
  EXPECT_FALSE(called);
}

TEST(ScopedTaskRunnerTest, MakeScopedAfterShutdown) {
  bool called = false;
  ScopedTaskRunner tasks;
  tasks.ShutDown();
  tasks.MakeScoped([&] { called = true; })();
  EXPECT_FALSE(called);
}

TEST(ScopedTaskRunnerTest, DestroyDuringTaskOnThread) {
  bool called = false;
  auto* tasks = new ScopedTaskRunner;
  tasks->MakeScoped([&] {
    delete tasks;
    called = true;
  })();
  EXPECT_TRUE(called);
}

TEST(ScopedTaskRunner, PostTaskAndFriends) {
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

TEST(ScopedTaskRunner, PostTaskAndFriendsCancelOnDeletion) {
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

TEST(ScopedTaskRunnerTest, PostPeriodicTask) {
  async::TestLoop loop;
  int called = 0;
  {
    ScopedTaskRunner tasks(loop.dispatcher());
    tasks.PostPeriodicTask([&] { called++; }, kInterval, false);
    loop.RunFor(kInterval * 4);
  }

  EXPECT_EQ(4, called);

  loop.RunFor(kInterval * 4);
  EXPECT_EQ(4, called);
}

}  // namespace
}  // namespace callback
