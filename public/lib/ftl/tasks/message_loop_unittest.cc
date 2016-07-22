// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/tasks/message_loop.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/ftl/functional/wrap_lambda.h"
#include "lib/ftl/macros.h"

namespace ftl {
namespace {

TEST(MessageLoop, CanRunTasks) {
  bool did_run = false;
  MessageLoop loop;
  loop.task_runner()->PostTask([&did_run, &loop]() {
    EXPECT_FALSE(did_run);
    did_run = true;
    loop.QuitNow();
  });
  loop.Run();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, CanPostTasksFromTasks) {
  bool did_run = false;
  MessageLoop loop;
  Closure nested_task = [&did_run, &loop]() {
    EXPECT_FALSE(did_run);
    did_run = true;
    loop.QuitNow();
  };
  loop.task_runner()->PostTask(
      [&nested_task, &loop]() { loop.task_runner()->PostTask(nested_task); });
  loop.Run();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, CanRunTasksInOrder) {
  std::vector<std::string> tasks;
  MessageLoop loop;
  loop.task_runner()->PostTask([&tasks]() { tasks.push_back("0"); });
  loop.task_runner()->PostTask([&tasks]() { tasks.push_back("1"); });
  loop.task_runner()->PostTask([&loop]() { loop.QuitNow(); });
  loop.task_runner()->PostTask([&tasks]() { tasks.push_back("2"); });
  loop.Run();
  EXPECT_EQ(2u, tasks.size());
  EXPECT_EQ("0", tasks[0]);
  EXPECT_EQ("1", tasks[1]);
}

TEST(MessageLoop, CanPreloadTasks) {
  auto incoming_queue = MakeRefCounted<internal::IncomingTaskQueue>();

  bool did_run = false;
  MessageLoop* loop_ptr = nullptr;
  incoming_queue->PostTask([&did_run, &loop_ptr]() {
    EXPECT_FALSE(did_run);
    did_run = true;
    loop_ptr->QuitNow();
  });

  MessageLoop loop(std::move(incoming_queue));
  loop_ptr = &loop;
  loop.Run();
  EXPECT_TRUE(did_run);
}

struct DestructorObserver {
  DestructorObserver(bool* destructed) : destructed_(destructed) {
    *destructed_ = false;
  }
  ~DestructorObserver() { *destructed_ = true; }

  bool* destructed_;
};

TEST(MessageLoop, TaskDestructionTime) {
  bool destructed = false;
  RefPtr<TaskRunner> task_runner;

  {
    MessageLoop loop;
    task_runner = RefPtr<TaskRunner>(loop.task_runner());
    task_runner->PostTask([&loop]() { loop.QuitNow(); });
    loop.Run();
    auto observer1 = std::make_unique<DestructorObserver>(&destructed);
    task_runner->PostTask(WrapLambda([p = std::move(observer1)](){}));
    EXPECT_FALSE(destructed);
  }

  EXPECT_TRUE(destructed);
  auto observer2 = std::make_unique<DestructorObserver>(&destructed);
  EXPECT_FALSE(destructed);
  task_runner->PostTask(WrapLambda([p = std::move(observer2)](){}));
  EXPECT_TRUE(destructed);
}

TEST(MessageLoop, CanQuitCurrent) {
  bool did_run = false;
  MessageLoop loop;
  loop.task_runner()->PostTask([&did_run]() {
    did_run = true;
    MessageLoop::GetCurrent()->QuitNow();
  });
  loop.Run();
  EXPECT_TRUE(did_run);
}

}  // namespace
}  // namespace ftl
