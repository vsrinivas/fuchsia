// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"

#include <zx/channel.h>
#include <zx/event.h>
#include <fdio/io.h>

#include <poll.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/fd_waiter.h"

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
    loop.QuitNow();
  });
  loop.Run();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, CanPostTasksFromTasks) {
  bool did_run = false;
  MessageLoop loop;
  fxl::Closure nested_task = [&did_run, &loop]() {
    EXPECT_FALSE(did_run);
    did_run = true;
    loop.QuitNow();
  };
  loop.task_runner()->PostTask(
      [&nested_task, &loop]() { loop.task_runner()->PostTask(nested_task); });
  loop.Run();
  EXPECT_TRUE(did_run);
}

TEST(MessageLoop, TriplyNestedTasks) {
  std::vector<std::string> tasks;
  MessageLoop loop;
  loop.task_runner()->PostTask([&tasks, &loop]() {
    tasks.push_back("one");
    loop.task_runner()->PostTask([&tasks, &loop]() {
      tasks.push_back("two");
      loop.task_runner()->PostTask([&tasks, &loop]() {
        tasks.push_back("three");
        loop.QuitNow();
      });
    });
  });
  loop.Run();
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
  loop.Run();
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
    loop_ptr->QuitNow();
  });

  MessageLoop loop(std::move(incoming_queue));
  loop_ptr = &loop;
  loop.Run();
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
  loop.Run();
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
  loop.PostQuitTask();
  loop.Run();
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
    loop.PostQuitTask();
    loop.Run();
    auto observer1 = std::make_unique<DestructorObserver>(
        [&destructed] { destructed = true; });
    task_runner->PostTask(fxl::MakeCopyable([p = std::move(observer1)](){}));
    EXPECT_FALSE(destructed);
  }
  EXPECT_TRUE(destructed);

  destructed = false;
  auto observer2 = std::make_unique<DestructorObserver>(
      [&destructed] { destructed = true; });
  task_runner->PostTask(fxl::MakeCopyable([p = std::move(observer2)](){}));
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

TEST(MessageLoop, CanQuitManyTimes) {
  MessageLoop loop;
  loop.QuitNow();
  loop.QuitNow();
  loop.PostQuitTask();
  loop.Run();
  loop.QuitNow();
  loop.QuitNow();
}

class TestMessageLoopHandler : public MessageLoopHandler {
 public:
  TestMessageLoopHandler() {}
  ~TestMessageLoopHandler() override {}

  void clear_ready_count() { ready_count_ = 0; }
  int ready_count() const { return ready_count_; }

  void clear_error_count() { error_count_ = 0; }
  int error_count() const { return error_count_; }

  zx_status_t last_error_result() const { return last_error_result_; }

  // MessageLoopHandler:
  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override {
    ready_count_++;
  }
  void OnHandleError(zx_handle_t handle, zx_status_t status) override {
    error_count_++;
    last_error_result_ = status;
  }

 private:
  int ready_count_ = 0;
  int error_count_ = 0;
  zx_status_t last_error_result_ = ZX_OK;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestMessageLoopHandler);
};

class QuitOnReadyMessageLoopHandler : public TestMessageLoopHandler {
 public:
  QuitOnReadyMessageLoopHandler() {}
  ~QuitOnReadyMessageLoopHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override {
    message_loop_->QuitNow();
    TestMessageLoopHandler::OnHandleReady(handle, pending, count);
  }

 private:
  MessageLoop* message_loop_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(QuitOnReadyMessageLoopHandler);
};

// Verifies Quit() from OnHandleReady() quits the loop.
TEST(MessageLoop, QuitFromReady) {
  QuitOnReadyMessageLoopHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);
  zx_status_t rv = endpoint1.write(0, nullptr, 0, nullptr, 0);
  EXPECT_EQ(ZX_OK, rv);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, endpoint0.get(), ZX_CHANNEL_READABLE, fxl::TimeDelta::Max());
  message_loop.Run();
  EXPECT_EQ(1, handler.ready_count());
  EXPECT_EQ(0, handler.error_count());
  EXPECT_TRUE(message_loop.HasHandler(key));
}

class RemoveOnReadyMessageLoopHandler : public TestMessageLoopHandler {
 public:
  RemoveOnReadyMessageLoopHandler() {}
  ~RemoveOnReadyMessageLoopHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void set_handler_key(MessageLoop::HandlerKey key) { key_ = key; }

  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override {
    EXPECT_TRUE(message_loop_->HasHandler(key_));
    message_loop_->RemoveHandler(key_);
    TestMessageLoopHandler::OnHandleReady(handle, pending, count);
    MessageLoop::GetCurrent()->PostQuitTask();
  }

 private:
  MessageLoop* message_loop_ = nullptr;
  MessageLoop::HandlerKey key_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoveOnReadyMessageLoopHandler);
};

TEST(MessageLoop, HandleReady) {
  RemoveOnReadyMessageLoopHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);
  zx_status_t rv = endpoint1.write(0, nullptr, 0, nullptr, 0);
  EXPECT_EQ(ZX_OK, rv);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, endpoint0.get(), ZX_CHANNEL_READABLE, fxl::TimeDelta::Max());
  handler.set_handler_key(key);
  message_loop.Run();
  EXPECT_EQ(1, handler.ready_count());
  EXPECT_EQ(0, handler.error_count());
  EXPECT_FALSE(message_loop.HasHandler(key));
}

TEST(MessageLoop, AfterHandleReadyCallback) {
  RemoveOnReadyMessageLoopHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);
  zx_status_t rv = endpoint1.write(0, nullptr, 0, nullptr, 0);
  EXPECT_EQ(ZX_OK, rv);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, endpoint0.get(), ZX_CHANNEL_READABLE, fxl::TimeDelta::Max());
  handler.set_handler_key(key);
  int after_task_callback_count = 0;
  message_loop.SetAfterTaskCallback(
      [&after_task_callback_count] { ++after_task_callback_count; });
  message_loop.Run();
  EXPECT_EQ(1, handler.ready_count());
  EXPECT_EQ(0, handler.error_count());
  EXPECT_EQ(2, after_task_callback_count);
  EXPECT_FALSE(message_loop.HasHandler(key));
}

TEST(MessageLoop, AfterDeadlineExpiredCallback) {
  TestMessageLoopHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);

  MessageLoop message_loop;
  message_loop.AddHandler(&handler, endpoint0.get(), ZX_CHANNEL_READABLE,
                          fxl::TimeDelta::FromMicroseconds(10000));
  message_loop.task_runner()->PostDelayedTask(
      [&message_loop] { message_loop.QuitNow(); },
      fxl::TimeDelta::FromMicroseconds(15000));
  int after_task_callback_count = 0;
  message_loop.SetAfterTaskCallback(
      [&after_task_callback_count] { ++after_task_callback_count; });
  message_loop.Run();
  EXPECT_EQ(2, after_task_callback_count);
}

class QuitOnErrorRunMessageHandler : public TestMessageLoopHandler {
 public:
  QuitOnErrorRunMessageHandler() {}
  ~QuitOnErrorRunMessageHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void OnHandleError(zx_handle_t handle, zx_status_t status) override {
    message_loop_->QuitNow();
    TestMessageLoopHandler::OnHandleError(handle, status);
  }

 private:
  MessageLoop* message_loop_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(QuitOnErrorRunMessageHandler);
};

// Verifies Quit() when the deadline is reached works.
// Also ensures that handlers are removed after a timeout occurs.
TEST(MessageLoop, QuitWhenDeadlineExpired) {
  QuitOnErrorRunMessageHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key =
      message_loop.AddHandler(&handler, endpoint0.get(), ZX_CHANNEL_READABLE,
                              fxl::TimeDelta::FromMicroseconds(10000));
  message_loop.Run();
  EXPECT_EQ(0, handler.ready_count());
  EXPECT_EQ(1, handler.error_count());
  EXPECT_EQ(ZX_ERR_TIMED_OUT, handler.last_error_result());
  EXPECT_FALSE(message_loop.HasHandler(key));
}

// Test that handlers are notified of loop destruction.
TEST(MessageLoop, Destruction) {
  TestMessageLoopHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);
  {
    MessageLoop message_loop;
    message_loop.AddHandler(&handler, endpoint0.get(), ZX_CHANNEL_READABLE,
                            fxl::TimeDelta::Max());
  }
  EXPECT_EQ(1, handler.error_count());
  EXPECT_EQ(ZX_ERR_CANCELED, handler.last_error_result());
}

class RemoveManyMessageLoopHandler : public TestMessageLoopHandler {
 public:
  RemoveManyMessageLoopHandler() {}
  ~RemoveManyMessageLoopHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void add_handler_key(MessageLoop::HandlerKey key) { keys_.push_back(key); }

  void OnHandleError(zx_handle_t handle, zx_status_t status) override {
    for (auto key : keys_)
      message_loop_->RemoveHandler(key);
    TestMessageLoopHandler::OnHandleError(handle, status);
  }

 private:
  std::vector<MessageLoop::HandlerKey> keys_;
  MessageLoop* message_loop_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoveManyMessageLoopHandler);
};

class ChannelPair {
 public:
  ChannelPair() { zx::channel::create(0, &endpoint0, &endpoint1); }

  zx::channel endpoint0;
  zx::channel endpoint1;
};

// Test that handlers are notified of loop destruction.
TEST(MessageLoop, MultipleHandleDestruction) {
  RemoveManyMessageLoopHandler odd_handler;
  TestMessageLoopHandler even_handler;
  ChannelPair pipe1;
  ChannelPair pipe2;
  ChannelPair pipe3;
  {
    MessageLoop message_loop;
    odd_handler.set_message_loop(&message_loop);
    odd_handler.add_handler_key(
        message_loop.AddHandler(&odd_handler, pipe1.endpoint0.get(),
                                ZX_CHANNEL_READABLE, fxl::TimeDelta::Max()));
    message_loop.AddHandler(&even_handler, pipe2.endpoint0.get(),
                            ZX_CHANNEL_READABLE, fxl::TimeDelta::Max());
    odd_handler.add_handler_key(
        message_loop.AddHandler(&odd_handler, pipe3.endpoint0.get(),
                                ZX_CHANNEL_READABLE, fxl::TimeDelta::Max()));
  }
  EXPECT_EQ(1, odd_handler.error_count());
  EXPECT_EQ(1, even_handler.error_count());
  EXPECT_EQ(ZX_ERR_CANCELED, odd_handler.last_error_result());
  EXPECT_EQ(ZX_ERR_CANCELED, even_handler.last_error_result());
}

class AddHandlerOnErrorHandler : public TestMessageLoopHandler {
 public:
  AddHandlerOnErrorHandler() {}
  ~AddHandlerOnErrorHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void OnHandleError(zx_handle_t handle, zx_status_t status) override {
    message_loop_->AddHandler(this, handle, ZX_CHANNEL_READABLE,
                              fxl::TimeDelta::Max());
    TestMessageLoopHandler::OnHandleError(handle, status);
  }

 private:
  MessageLoop* message_loop_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddHandlerOnErrorHandler);
};

// Ensures that the MessageLoop doesn't get into infinite loops if
// new handlers are added while canceling handlers during MessageLoop
// destruction.
TEST(MessageLoop, AddHandlerOnError) {
  AddHandlerOnErrorHandler handler;
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);
  {
    MessageLoop message_loop;
    handler.set_message_loop(&message_loop);
    message_loop.AddHandler(&handler, endpoint0.get(), ZX_CHANNEL_READABLE,
                            fxl::TimeDelta::Max());
  }
  EXPECT_EQ(1, handler.error_count());
  EXPECT_EQ(ZX_ERR_CANCELED, handler.last_error_result());
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

class ErrorHandler : public MessageLoopHandler {
 public:
  using Callback = std::function<void(zx_handle_t, zx_status_t)>;

  ErrorHandler(Callback callback) : callback_(std::move(callback)) {}

  void OnHandleError(zx_handle_t handle, zx_status_t error) override {
    callback_(handle, error);
  }

 private:
  Callback callback_;
};

// Tests that the message loop's task runner can still be accessed during
// message loop destruction (while tearing down remaining tasks and handlers)
// though any tasks posted to it are immediately destroyed.
TEST(MessageLoop, TaskRunnerAvailableDuringLoopDestruction) {
  zx::event event;
  EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));

  auto loop = std::make_unique<MessageLoop>();

  // Set up a handler which will record some observed state then attempt to
  // post a task.  The task should be destroyed immediately without running.
  zx_handle_t handler_observed_handle = ZX_HANDLE_INVALID;
  zx_status_t handler_observed_error = ZX_OK;
  bool handler_observed_runs_tasks_on_current_thread = false;
  bool task_posted_from_handler_destroyed = false;
  bool task_posted_from_handler_ran = false;
  ErrorHandler handler([
    task_runner = loop->task_runner(),               //
    &handler_observed_handle,                        //
    &handler_observed_error,                         //
    &handler_observed_runs_tasks_on_current_thread,  //
    &task_posted_from_handler_ran,                   //
    &task_posted_from_handler_destroyed
  ](zx_handle_t handle, zx_status_t error) {
    handler_observed_handle = handle;
    handler_observed_error = error;
    handler_observed_runs_tasks_on_current_thread =
        task_runner->RunsTasksOnCurrentThread();
    task_runner->PostTask([
      &task_posted_from_handler_ran,
      d = DestructorObserver([&task_posted_from_handler_destroyed] {
        task_posted_from_handler_destroyed = true;
      })
    ] { task_posted_from_handler_ran = true; });
  });

  // Set up a task which will record some observed state during destruction
  // then attempt to post another task.  The task should be destroyed
  // immediately without running.
  bool task_destroyed = false;
  bool task_observed_runs_tasks_on_current_thread = false;
  bool task_posted_from_task_ran = false;
  bool task_posted_from_task_destroyed = false;
  fxl::Closure task =
      [d = DestructorObserver([
         task_runner = loop->task_runner(),            //
         &task_observed_runs_tasks_on_current_thread,  //
         &task_destroyed,                              //
         &task_posted_from_task_ran,                   //
         &task_posted_from_task_destroyed
       ] {
         task_destroyed = true;
         task_observed_runs_tasks_on_current_thread =
             task_runner->RunsTasksOnCurrentThread();
         task_runner->PostTask([
           &task_posted_from_task_ran,
           d = DestructorObserver([&task_posted_from_task_destroyed] {
             task_posted_from_task_destroyed = true;
           })
         ] { task_posted_from_task_ran = true; });
       })]{};

  loop->task_runner()->PostTask(std::move(task));
  loop->AddHandler(&handler, event.get(), ZX_EVENT_SIGNALED);
  loop.reset();

  EXPECT_EQ(event.get(), handler_observed_handle);
  EXPECT_EQ(ZX_ERR_CANCELED, handler_observed_error);
  EXPECT_TRUE(handler_observed_runs_tasks_on_current_thread);
  EXPECT_FALSE(task_posted_from_handler_ran);
  EXPECT_TRUE(task_posted_from_handler_destroyed);

  EXPECT_TRUE(task_destroyed);
  EXPECT_TRUE(task_observed_runs_tasks_on_current_thread);
  EXPECT_FALSE(task_posted_from_task_ran);
  EXPECT_TRUE(task_posted_from_task_destroyed);
}

}  // namespace
}  // namespace fsl
