// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/scoped_task_runner.h"

#include <lib/async-testing/test_loop.h>
#include <pthread.h>

#include <thread>

#include "gtest/gtest.h"

namespace ledger {
namespace {

constexpr zx::duration kInterval = zx::min(4);  // to save the world

struct MoveOnly {
  MoveOnly(MoveOnly&&) = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  bool called;
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
  tasks.MakeScoped([&called, move_only = std::move(move_only)] { called = move_only.called; })();
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

TEST(ScopedTaskRunnerTest, Reset) {
  bool beforeCalled = false, afterCalled = false;
  ScopedTaskRunner tasks;
  auto before = tasks.MakeScoped([&] { beforeCalled = true; });
  tasks.Reset();
  auto after = tasks.MakeScoped([&] { afterCalled = true; });
  before();
  after();
  EXPECT_FALSE(beforeCalled);
  EXPECT_TRUE(afterCalled);
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

  called = 0;
  loop.RunFor(kInterval * 4);
  EXPECT_EQ(0, called);
}

// Verifies that all occurrences of a periodic task posted from a non-dispatch
// thread execute on the dispatch thread.
//
// Example bug:
// https://fuchsia.googlesource.com/garnet/+/3a8dab9a939efeeb5e18454595a558f9972f7a42/public/lib/callback/scoped_task_runner.cc#47
TEST(ScopedTaskRunnerTest, PostPeriodicTaskOffThread) {
  async::TestLoop loop;
  ScopedTaskRunner tasks(loop.dispatcher());

  pthread_t dispatch_thread = pthread_self();

  int called = 0;
  std::thread foreign_thread([&] {
    // test-correctness assertion
    EXPECT_NE(dispatch_thread, pthread_self());

    tasks.PostPeriodicTask(
        [&] {
          ++called;
          EXPECT_EQ(dispatch_thread, pthread_self()) << "iteration " << called;
        },
        kInterval, true);
  });
  foreign_thread.join();

  loop.RunFor(kInterval * 4);

  EXPECT_EQ(5, called);
}

}  // namespace
}  // namespace ledger
