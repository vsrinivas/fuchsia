// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_domain.h"

#include <fbl/ref_counted.h>

#include "gtest/gtest.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"

namespace btlib {
namespace common {
namespace {

class TestObject : public fbl::RefCounted<TestObject>,
                   public TaskDomain<TestObject> {
 public:
  // TestObject gets handed a TaskRunner and does not own the thread.
  explicit TestObject(fxl::RefPtr<fxl::TaskRunner> task_runner)
      : TaskDomain<TestObject>(this, task_runner) {}

  void ScheduleTask() {
    PostMessage([this] {
      EXPECT_TRUE(task_runner()->RunsTasksOnCurrentThread());

      {
        std::lock_guard<std::mutex> lock(mtx);
        task_done = true;
      }

      cv.notify_one();
    });
  }

  void ShutDown() { TaskDomain<TestObject>::ScheduleCleanUp(); }

  void CleanUp() {
    EXPECT_TRUE(task_runner()->RunsTasksOnCurrentThread());
    cleaned_up = true;
  }

  std::mutex mtx;
  std::condition_variable cv;

  bool task_done = false;
  bool cleaned_up = false;
};

TEST(TaskDomainTest, PostMessageAndCleanUp) {
  fxl::RefPtr<fxl::TaskRunner> thrd_runner;
  std::thread thrd = fsl::CreateThread(&thrd_runner, "task_domain_unittest");
  ASSERT_TRUE(thrd_runner);

  auto obj = fbl::AdoptRef(new TestObject(thrd_runner));

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
  obj->ShutDown();
  obj->ScheduleTask();

  bool done = false;
  thrd_runner->PostTask([obj, &done] {
    fsl::MessageLoop::GetCurrent()->QuitNow();

    {
      std::lock_guard<std::mutex> lock(obj->mtx);
      done = true;
    }

    obj->cv.notify_one();
  });

  // Wait for the shut down task to finish running.
  obj->cv.wait(lock, [&done] { return done; });

  EXPECT_TRUE(obj->cleaned_up);
  EXPECT_FALSE(obj->task_done);

  if (thrd.joinable())
    thrd.join();
}

}  // namespace
}  // namespace common
}  // namespace btlib
