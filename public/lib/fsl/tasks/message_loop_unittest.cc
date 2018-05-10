// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"

#include <fdio/io.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <poll.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"

namespace fsl {
namespace {

TEST(MessageLoop, Current) {
  EXPECT_TRUE(MessageLoop::GetCurrent() == nullptr);
  {
    MessageLoop message_loop;
    EXPECT_EQ(&message_loop, MessageLoop::GetCurrent());
  }
  EXPECT_TRUE(MessageLoop::GetCurrent() == nullptr);
}

TEST(MessageLoop, RunsTasksOnCurrentThread) {
  fxl::RefPtr<fxl::TaskRunner> task_runner;

  {
    MessageLoop loop;
    task_runner = loop.task_runner();
    EXPECT_TRUE(task_runner->RunsTasksOnCurrentThread());
    bool run_on_other_thread;
    std::thread t([task_runner, &run_on_other_thread]() {
      run_on_other_thread = task_runner->RunsTasksOnCurrentThread();
    });
    t.join();
    EXPECT_FALSE(run_on_other_thread);
  }

  EXPECT_FALSE(task_runner->RunsTasksOnCurrentThread());
}

TEST(MessageLoop, CanRunTasks) {
  bool did_run = false;
  MessageLoop loop;
  loop.task_runner()->PostTask([&did_run, &loop]() {
    EXPECT_FALSE(did_run);
    did_run = true;
  });
  loop.RunUntilIdle();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, CanPostTasksFromTasks) {
  bool did_run = false;
  MessageLoop loop;
  fxl::Closure nested_task = [&did_run, &loop]() {
    EXPECT_FALSE(did_run);
    did_run = true;
  };
  loop.task_runner()->PostTask(
      [&nested_task, &loop]() { loop.task_runner()->PostTask(nested_task); });
  loop.RunUntilIdle();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, TriplyNestedTasks) {
  std::vector<std::string> tasks;
  MessageLoop loop;
  loop.task_runner()->PostTask([&tasks, &loop]() {
    tasks.push_back("one");
    loop.task_runner()->PostTask([&tasks, &loop]() {
      tasks.push_back("two");
      loop.task_runner()->PostTask(
          [&tasks, &loop]() { tasks.push_back("three"); });
    });
  });
  loop.RunUntilIdle();
  EXPECT_EQ(3u, tasks.size());
  EXPECT_EQ("one", tasks[0]);
  EXPECT_EQ("two", tasks[1]);
  EXPECT_EQ("three", tasks[2]);
}

TEST(MessageLoop, CanRunTasksInOrder) {
  std::vector<std::string> tasks;
  MessageLoop loop;
  loop.task_runner()->PostTask([&tasks]() { tasks.push_back("0"); });
  loop.task_runner()->PostTask([&tasks]() { tasks.push_back("1"); });
  loop.PostQuitTask();
  loop.task_runner()->PostTask([&tasks]() { tasks.push_back("2"); });
  loop.RunUntilIdle();
  EXPECT_EQ(2u, tasks.size());
  EXPECT_EQ("0", tasks[0]);
  EXPECT_EQ("1", tasks[1]);
}

TEST(MessageLoop, CanPreloadTasks) {
  auto incoming_queue = fxl::MakeRefCounted<internal::IncomingTaskQueue>();

  bool did_run = false;
  MessageLoop* loop_ptr = nullptr;
  incoming_queue->PostTask([&did_run, &loop_ptr]() {
    EXPECT_FALSE(did_run);
    did_run = true;
  });

  MessageLoop loop(std::move(incoming_queue));
  loop_ptr = &loop;
  loop.RunUntilIdle();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, AfterTaskCallbacks) {
  std::vector<std::string> tasks;
  MessageLoop loop;
  loop.SetAfterTaskCallback([&tasks] { tasks.push_back("callback"); });
  loop.task_runner()->PostTask([&tasks] { tasks.push_back("0"); });
  loop.task_runner()->PostTask([&tasks] { tasks.push_back("1"); });
  loop.PostQuitTask();
  loop.task_runner()->PostTask([&tasks] { tasks.push_back("2"); });
  loop.RunUntilIdle();
  EXPECT_EQ(5u, tasks.size());
  EXPECT_EQ("0", tasks[0]);
  EXPECT_EQ("callback", tasks[1]);
  EXPECT_EQ("1", tasks[2]);
  EXPECT_EQ("callback", tasks[3]);
}

TEST(MessageLoop, RemoveAfterTaskCallbacksDuringCallback) {
  std::vector<std::string> tasks;
  MessageLoop loop;

  loop.SetAfterTaskCallback([&tasks, &loop]() {
    tasks.push_back("callback");
    loop.ClearAfterTaskCallback();
  });
  loop.task_runner()->PostTask([&tasks] { tasks.push_back("0"); });
  loop.task_runner()->PostTask([&tasks] { tasks.push_back("1"); });
  loop.RunUntilIdle();
  EXPECT_EQ(3u, tasks.size());
  EXPECT_EQ("0", tasks[0]);
  EXPECT_EQ("callback", tasks[1]);
  EXPECT_EQ("1", tasks[2]);
}

class DestructorObserver {
 public:
  DestructorObserver(fxl::Closure callback) : callback_(std::move(callback)) {}
  ~DestructorObserver() { callback_(); }

 private:
  fxl::Closure callback_;
};

TEST(MessageLoop, TaskDestructionTime) {
  bool destructed = false;
  fxl::RefPtr<fxl::TaskRunner> task_runner;

  {
    MessageLoop loop;
    task_runner = fxl::RefPtr<fxl::TaskRunner>(loop.task_runner());
    loop.RunUntilIdle();
    auto observer1 = std::make_unique<DestructorObserver>(
        [&destructed] { destructed = true; });
    task_runner->PostTask(fxl::MakeCopyable([p = std::move(observer1)]() {}));
    EXPECT_FALSE(destructed);
  }
  EXPECT_TRUE(destructed);

  destructed = false;
  auto observer2 = std::make_unique<DestructorObserver>(
      [&destructed] { destructed = true; });
  task_runner->PostTask(fxl::MakeCopyable([p = std::move(observer2)]() {}));
  EXPECT_TRUE(destructed);
}

TEST(MessageLoop, CanQuitCurrent) {
  int count = 0;
  MessageLoop loop;
  loop.task_runner()->PostTask([&count]() {
    count++;
    MessageLoop::GetCurrent()->QuitNow();
  });
  loop.task_runner()->PostTask([&count]() { count++; });
  loop.RunUntilIdle();
  EXPECT_EQ(1, count);
}

TEST(MessageLoop, CanQuitManyTimes) {
  MessageLoop loop;
  loop.QuitNow();
  loop.QuitNow();
  loop.PostQuitTask();
  loop.RunUntilIdle();
  loop.QuitNow();
  loop.QuitNow();
}

// Tests that waiting on files in a MessageLoop works.
TEST(MessageLoop, FDWaiter) {
  // Create an event and an FD that reflects that event. The fd
  // shares ownership of the event.
  zx::event fdevent;
  EXPECT_EQ(zx::event::create(0u, &fdevent), ZX_OK);
  fxl::UniqueFD fd(
      fdio_handle_fd(fdevent.get(), ZX_USER_SIGNAL_0, 0, /*shared=*/true));
  EXPECT_TRUE(fd.is_valid());

  bool callback_ran = false;
  {
    MessageLoop message_loop;
    FDWaiter waiter;

    std::thread thread([&fdevent]() {
      // Poke the fdevent, which pokes the fd.
      EXPECT_EQ(fdevent.signal(0u, ZX_USER_SIGNAL_0), ZX_OK);
    });
    auto callback = [&callback_ran, &message_loop](zx_status_t success,
                                                   uint32_t events) {
      EXPECT_EQ(success, ZX_OK);
      EXPECT_EQ(events, static_cast<uint32_t>(POLLIN));
      callback_ran = true;
      message_loop.QuitNow();
    };
    EXPECT_TRUE(waiter.Wait(callback, fd.get(), POLLIN));
    message_loop.Run();
    thread.join();
  }

  EXPECT_TRUE(callback_ran);
}

// Tests that the message loop's task runner can still be accessed during
// message loop destruction (while tearing down remaining tasks and handlers)
// though any tasks posted to it are immediately destroyed.
TEST(MessageLoop, TaskRunnerAvailableDuringLoopDestruction) {
  zx::event event;
  EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));

  auto loop = std::make_unique<MessageLoop>();

  // Set up a task which will record some observed state during destruction
  // then attempt to post another task.  The task should be destroyed
  // immediately without running.
  bool task_destroyed = false;
  bool task_observed_runs_tasks_on_current_thread = false;
  bool task_posted_from_task_ran = false;
  bool task_posted_from_task_destroyed = false;
  fxl::Closure task =
      [d = DestructorObserver([task_runner = loop->task_runner(),            //
                               &task_observed_runs_tasks_on_current_thread,  //
                               &task_destroyed,                              //
                               &task_posted_from_task_ran,                   //
                               &task_posted_from_task_destroyed] {
         task_destroyed = true;
         task_observed_runs_tasks_on_current_thread =
             task_runner->RunsTasksOnCurrentThread();
         task_runner->PostTask(
             [&task_posted_from_task_ran,
              d = DestructorObserver([&task_posted_from_task_destroyed] {
                task_posted_from_task_destroyed = true;
              })] { task_posted_from_task_ran = true; });
       })] {};

  loop->task_runner()->PostTask(std::move(task));
  loop.reset();

  EXPECT_TRUE(task_destroyed);
  EXPECT_TRUE(task_observed_runs_tasks_on_current_thread);
  EXPECT_FALSE(task_posted_from_task_ran);
  EXPECT_TRUE(task_posted_from_task_destroyed);
}

}  // namespace
}  // namespace fsl
