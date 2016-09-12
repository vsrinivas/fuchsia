// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/message_loop.h"

#include <mojo/system/result.h>

#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace mtl {
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
  MessageLoop loop;
  EXPECT_TRUE(loop.task_runner()->RunsTasksOnCurrentThread());
  bool run_on_other_thread;
  std::thread t([&loop, &run_on_other_thread]() {
    run_on_other_thread = loop.task_runner()->RunsTasksOnCurrentThread();
  });
  t.join();
  EXPECT_FALSE(run_on_other_thread);
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
  ftl::Closure nested_task = [&did_run, &loop]() {
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
  auto incoming_queue = ftl::MakeRefCounted<internal::IncomingTaskQueue>();

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
  EXPECT_EQ(4u, tasks.size());
  EXPECT_EQ("0", tasks[0]);
  EXPECT_EQ("callback", tasks[1]);
  EXPECT_EQ("1", tasks[2]);
  EXPECT_EQ("callback", tasks[3]);
  // Notice that the callback doesn't run after the quit task because we're
  // quitting.
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

struct DestructorObserver {
  DestructorObserver(bool* destructed) : destructed_(destructed) {
    *destructed_ = false;
  }
  ~DestructorObserver() { *destructed_ = true; }

  bool* destructed_;
};

TEST(MessageLoop, TaskDestructionTime) {
  bool destructed = false;
  ftl::RefPtr<ftl::TaskRunner> task_runner;

  {
    MessageLoop loop;
    task_runner = ftl::RefPtr<ftl::TaskRunner>(loop.task_runner());
    loop.PostQuitTask();
    loop.Run();
    auto observer1 = std::make_unique<DestructorObserver>(&destructed);
    task_runner->PostTask(ftl::MakeCopyable([p = std::move(observer1)](){}));
    EXPECT_FALSE(destructed);
  }

  EXPECT_TRUE(destructed);
  auto observer2 = std::make_unique<DestructorObserver>(&destructed);
  EXPECT_FALSE(destructed);
  task_runner->PostTask(ftl::MakeCopyable([p = std::move(observer2)](){}));
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

class TestMessageLoopHandler : public MessageLoopHandler {
 public:
  TestMessageLoopHandler() {}
  ~TestMessageLoopHandler() override {}

  void clear_ready_count() { ready_count_ = 0; }
  int ready_count() const { return ready_count_; }

  void clear_error_count() { error_count_ = 0; }
  int error_count() const { return error_count_; }

  MojoResult last_error_result() const { return last_error_result_; }

  // MessageLoopHandler:
  void OnHandleReady(MojoHandle handle) override { ready_count_++; }
  void OnHandleError(MojoHandle handle, MojoResult status) override {
    error_count_++;
    last_error_result_ = status;
  }

 private:
  int ready_count_ = 0;
  int error_count_ = 0;
  MojoResult last_error_result_ = MOJO_RESULT_OK;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestMessageLoopHandler);
};

class QuitOnReadyMessageLoopHandler : public TestMessageLoopHandler {
 public:
  QuitOnReadyMessageLoopHandler() {}
  ~QuitOnReadyMessageLoopHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void OnHandleReady(MojoHandle handle) override {
    message_loop_->QuitNow();
    TestMessageLoopHandler::OnHandleReady(handle);
  }

 private:
  MessageLoop* message_loop_ = nullptr;

  FTL_DISALLOW_COPY_AND_ASSIGN(QuitOnReadyMessageLoopHandler);
};

// Verifies Quit() from OnHandleReady() quits the loop.
TEST(MessageLoop, QuitFromReady) {
  QuitOnReadyMessageLoopHandler handler;
  mojo::MessagePipe pipe;
  MojoResult result =
      mojo::WriteMessageRaw(pipe.handle1.get(), nullptr, 0, nullptr, 0, 0);
  EXPECT_EQ(MOJO_RESULT_OK, result);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, pipe.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
      ftl::TimeDelta::Max());
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

  void OnHandleReady(MojoHandle handle) override {
    message_loop_->RemoveHandler(key_);
    TestMessageLoopHandler::OnHandleReady(handle);
    MessageLoop::GetCurrent()->PostQuitTask();
  }

 private:
  MessageLoop* message_loop_ = nullptr;
  MessageLoop::HandlerKey key_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(RemoveOnReadyMessageLoopHandler);
};

TEST(MessageLoop, HandleReady) {
  RemoveOnReadyMessageLoopHandler handler;
  mojo::MessagePipe pipe;
  MojoResult result =
      mojo::WriteMessageRaw(pipe.handle1.get(), nullptr, 0, nullptr, 0, 0);
  EXPECT_EQ(MOJO_RESULT_OK, result);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, pipe.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
      ftl::TimeDelta::Max());
  handler.set_handler_key(key);
  message_loop.Run();
  EXPECT_EQ(1, handler.ready_count());
  EXPECT_EQ(0, handler.error_count());
  EXPECT_FALSE(message_loop.HasHandler(key));
}

TEST(MessageLoop, AfterHandleReadyCallback) {
  RemoveOnReadyMessageLoopHandler handler;
  mojo::MessagePipe pipe;
  MojoResult result =
      mojo::WriteMessageRaw(pipe.handle1.get(), nullptr, 0, nullptr, 0, 0);
  EXPECT_EQ(MOJO_RESULT_OK, result);

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, pipe.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
      ftl::TimeDelta::Max());
  handler.set_handler_key(key);
  int after_task_callback_count = 0;
  message_loop.SetAfterTaskCallback(
      [&after_task_callback_count] { ++after_task_callback_count; });
  message_loop.Run();
  EXPECT_EQ(1, handler.ready_count());
  EXPECT_EQ(0, handler.error_count());
  EXPECT_EQ(1, after_task_callback_count);
  EXPECT_FALSE(message_loop.HasHandler(key));
}

TEST(MessageLoop, AfterDeadlineExpiredCallback) {
  TestMessageLoopHandler handler;
  mojo::MessagePipe pipe;

  MessageLoop message_loop;
  message_loop.AddHandler(&handler, pipe.handle0.get().value(),
                          MOJO_HANDLE_SIGNAL_READABLE,
                          ftl::TimeDelta::FromMicroseconds(10000));
  message_loop.task_runner()->PostDelayedTask(
      [&message_loop] { message_loop.QuitNow(); },
      ftl::TimeDelta::FromMicroseconds(15000));
  int after_task_callback_count = 0;
  message_loop.SetAfterTaskCallback(
      [&after_task_callback_count] { ++after_task_callback_count; });
  message_loop.Run();
  EXPECT_EQ(1, after_task_callback_count);
}

class QuitOnErrorRunMessageHandler : public TestMessageLoopHandler {
 public:
  QuitOnErrorRunMessageHandler() {}
  ~QuitOnErrorRunMessageHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void OnHandleError(MojoHandle handle, MojoResult result) override {
    message_loop_->QuitNow();
    TestMessageLoopHandler::OnHandleError(handle, result);
  }

 private:
  MessageLoop* message_loop_ = nullptr;

  FTL_DISALLOW_COPY_AND_ASSIGN(QuitOnErrorRunMessageHandler);
};

// Verifies Quit() when the deadline is reached works.
TEST(MessageLoop, QuitWhenDeadlineExpired) {
  QuitOnErrorRunMessageHandler handler;
  mojo::MessagePipe pipe;

  MessageLoop message_loop;
  handler.set_message_loop(&message_loop);
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, pipe.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
      ftl::TimeDelta::FromMicroseconds(10000));
  message_loop.Run();
  EXPECT_EQ(0, handler.ready_count());
  EXPECT_EQ(1, handler.error_count());
  EXPECT_EQ(MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED, handler.last_error_result());
  EXPECT_FALSE(message_loop.HasHandler(key));
}

// Test that handlers are notified of loop destruction.
TEST(MessageLoop, Destruction) {
  TestMessageLoopHandler handler;
  mojo::MessagePipe pipe;
  {
    MessageLoop message_loop;
    message_loop.AddHandler(&handler, pipe.handle0.get().value(),
                            MOJO_HANDLE_SIGNAL_READABLE, ftl::TimeDelta::Max());
  }
  EXPECT_EQ(1, handler.error_count());
  EXPECT_EQ(MOJO_SYSTEM_RESULT_CANCELLED, handler.last_error_result());
}

class RemoveManyMessageLoopHandler : public TestMessageLoopHandler {
 public:
  RemoveManyMessageLoopHandler() {}
  ~RemoveManyMessageLoopHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void add_handler_key(MessageLoop::HandlerKey key) { keys_.push_back(key); }

  void OnHandleError(MojoHandle handle, MojoResult result) override {
    for (auto key : keys_)
      message_loop_->RemoveHandler(key);
    TestMessageLoopHandler::OnHandleError(handle, result);
  }

 private:
  std::vector<MessageLoop::HandlerKey> keys_;
  MessageLoop* message_loop_ = nullptr;

  FTL_DISALLOW_COPY_AND_ASSIGN(RemoveManyMessageLoopHandler);
};

// Test that handlers are notified of loop destruction.
TEST(MessageLoop, MultipleHandleDestruction) {
  RemoveManyMessageLoopHandler odd_handler;
  TestMessageLoopHandler even_handler;
  mojo::MessagePipe pipe1;
  mojo::MessagePipe pipe2;
  mojo::MessagePipe pipe3;
  {
    MessageLoop message_loop;
    odd_handler.set_message_loop(&message_loop);
    odd_handler.add_handler_key(message_loop.AddHandler(
        &odd_handler, pipe1.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
        ftl::TimeDelta::Max()));
    message_loop.AddHandler(&even_handler, pipe2.handle0.get().value(),
                            MOJO_HANDLE_SIGNAL_READABLE, ftl::TimeDelta::Max());
    odd_handler.add_handler_key(message_loop.AddHandler(
        &odd_handler, pipe3.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
        ftl::TimeDelta::Max()));
  }
  EXPECT_EQ(1, odd_handler.error_count());
  EXPECT_EQ(1, even_handler.error_count());
  EXPECT_EQ(MOJO_SYSTEM_RESULT_CANCELLED, odd_handler.last_error_result());
  EXPECT_EQ(MOJO_SYSTEM_RESULT_CANCELLED, even_handler.last_error_result());
}

class AddHandlerOnErrorHandler : public TestMessageLoopHandler {
 public:
  AddHandlerOnErrorHandler() {}
  ~AddHandlerOnErrorHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void OnHandleError(MojoHandle handle, MojoResult result) override {
    message_loop_->AddHandler(this, handle, MOJO_HANDLE_SIGNAL_READABLE,
                              ftl::TimeDelta::Max());
    TestMessageLoopHandler::OnHandleError(handle, result);
  }

 private:
  MessageLoop* message_loop_ = nullptr;

  FTL_DISALLOW_COPY_AND_ASSIGN(AddHandlerOnErrorHandler);
};

TEST(MessageLoop, AddHandlerOnError) {
  AddHandlerOnErrorHandler handler;
  mojo::MessagePipe pipe;
  {
    MessageLoop message_loop;
    handler.set_message_loop(&message_loop);
    message_loop.AddHandler(&handler, pipe.handle0.get().value(),
                            MOJO_HANDLE_SIGNAL_READABLE, ftl::TimeDelta::Max());
  }
  EXPECT_EQ(1, handler.error_count());
  EXPECT_EQ(MOJO_SYSTEM_RESULT_CANCELLED, handler.last_error_result());
}

class RemoveHandlerOnErrorHandler : public TestMessageLoopHandler {
 public:
  RemoveHandlerOnErrorHandler() {}
  ~RemoveHandlerOnErrorHandler() override {}

  void set_message_loop(MessageLoop* message_loop) {
    message_loop_ = message_loop;
  }

  void set_key_to_remove(MessageLoop::HandlerKey key) { key_to_remove_ = key; }

  void OnHandleError(MojoHandle handle, MojoResult result) override {
    message_loop_->RemoveHandler(key_to_remove_);
    TestMessageLoopHandler::OnHandleError(handle, result);
  }

 private:
  MessageLoop* message_loop_ = nullptr;
  MessageLoop::HandlerKey key_to_remove_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RemoveHandlerOnErrorHandler);
};

TEST(MessageLoop, AfterPreconditionFailedCallback) {
  RemoveHandlerOnErrorHandler handler;
  mojo::MessagePipe pipe;

  MessageLoop message_loop;
  MessageLoop::HandlerKey key = message_loop.AddHandler(
      &handler, pipe.handle0.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
      ftl::TimeDelta::Max());
  handler.set_message_loop(&message_loop);
  handler.set_key_to_remove(key);
  message_loop.task_runner()->PostTask([&pipe] { pipe.handle1.reset(); });
  message_loop.task_runner()->PostDelayedTask(
      [&message_loop] { message_loop.QuitNow(); },
      ftl::TimeDelta::FromMicroseconds(10000));
  int after_task_callback_count = 0;
  message_loop.SetAfterTaskCallback(
      [&after_task_callback_count] { ++after_task_callback_count; });
  message_loop.Run();
  EXPECT_EQ(2, after_task_callback_count);
}

}  // namespace
}  // namespace mtl
