// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_domain.h"

#include <fbl/ref_counted.h>
#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"

namespace btlib {
namespace common {
namespace {

class TestObject : public fbl::RefCounted<TestObject>,
                   public TaskDomain<TestObject> {
 public:
  // TestObject gets handed a an async dispatcher and does not own the thread.
  explicit TestObject(async_t* dispatcher)
      : TaskDomain<TestObject>(this, dispatcher) {}

  void ScheduleTask() {
    PostMessage([this] {
      AssertOnDispatcherThread();

      {
        std::lock_guard<std::mutex> lock(mtx);
        task_done = true;
      }

      cv.notify_one();
    });
  }

  void ShutDown() { TaskDomain<TestObject>::ScheduleCleanUp(); }

  void CleanUp() {
    AssertOnDispatcherThread();
    cleaned_up = true;
  }

  std::mutex mtx;
  std::condition_variable cv;

  bool task_done = false;
  bool cleaned_up = false;
};

TEST(TaskDomainTest, PostMessageAndCleanUp) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  loop.StartThread("task_domain_unittest");

  auto obj = fbl::AdoptRef(new TestObject(loop.async()));

  // Schedule a task. This is expected to run on the |thrd_runner|.
  obj->ScheduleTask();

  // Wait for the scheduled task to run.
  std::unique_lock<std::mutex> lock(obj->mtx);
  obj->cv.wait(lock, [obj] { return obj->task_done; });

  ASSERT_TRUE(obj->task_done);
  obj->task_done = false;

  // We schedule 3 tasks which will be run serially by |thrd_runner|. At the
  // time of the final quit task we expect the domain to be cleaned up which
  // should cause the second task to be dropped.

  // #1: clean up task. This won't quit the loop as the TaskDomain does not own
  // the thread.
  obj->ShutDown();

  // #2: This should not run due to #1.
  obj->ScheduleTask();

  // #3: This task quits the loop and blocks until all tasks have finished
  // running.
  loop.Shutdown();

  EXPECT_TRUE(obj->cleaned_up);
  EXPECT_FALSE(obj->task_done);
}

}  // namespace
}  // namespace common
}  // namespace btlib
