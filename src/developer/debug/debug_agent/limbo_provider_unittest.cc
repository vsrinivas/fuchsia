// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/limbo_provider.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/status.h>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/developer/debug/debug_agent/test_utils.h"
#include "src/lib/fxl/logging.h"

using namespace ::fuchsia::exception;

namespace debug_agent {
namespace {

// Stubs -------------------------------------------------------------------------------------------

class StubProcessLimbo : public fuchsia::exception::ProcessLimbo {
 public:
  void SetActive(bool active, SetActiveCallback cb) { FXL_NOTREACHED() << "Not needed for tests."; }

  void WatchActive(WatchActiveCallback callback) override {
    if (!reply_active_)
      return;

    callback(is_active_);
    reply_active_ = false;
  }

  void WatchProcessesWaitingOnException(
      ProcessLimbo::WatchProcessesWaitingOnExceptionCallback callback) override {
    watch_count_++;
    if (!reply_watch_processes_) {
      watch_processes_callback_ = std::move(callback);
      reply_watch_processes_ = true;
      return;
    }

    callback(fit::ok(CreateExceptionList()));
    reply_watch_processes_ = false;
  }

  void RetrieveException(zx_koid_t process_koid,
                         ProcessLimbo::RetrieveExceptionCallback callback) override {
    auto it = processes_.find(process_koid);
    if (it == processes_.end())
      return callback(fit::error(ZX_ERR_NOT_FOUND));

    // We cannot set any fake handles, as they will fail on the channel write.
    ProcessException exception;
    exception.set_info(it->second.info());

    processes_.erase(it);
    callback(fit::ok(std::move(exception)));
  }

  void ReleaseProcess(zx_koid_t process_koid, ProcessLimbo::ReleaseProcessCallback cb) override {
    auto it = processes_.find(process_koid);
    if (it == processes_.end())
      return cb(fit::error(ZX_ERR_NOT_FOUND));

    processes_.erase(it);
    cb(fit::ok());

    if (reply_watch_processes_ && watch_processes_callback_) {
      watch_processes_callback_(fit::ok(CreateExceptionList()));
      reply_watch_processes_ = false;
      return;
    }
  }

  void AppendException(const MockProcessObject& process, const MockThreadObject& thread,
                       ExceptionType exception_type) {
    ExceptionInfo info = {};
    info.process_koid = process.koid;
    info.thread_koid = thread.koid;
    info.type = exception_type;

    // Track the metadata in the limbo.
    ProcessExceptionMetadata metadata = {};
    metadata.set_info(std::move(info));

    // Sadly we cannot send bad handles over a channel, so we cannot actually send the "invented"
    // handles for this test. Setting the info is enough though.
    // metadata.set_process(process.GetHandle());
    // metadata.set_thread(thread.GetHandle());
    processes_[info.process_koid] = std::move(metadata);

    // If there is a callback, only send the new exceptions over.
    if (watch_processes_callback_) {
      watch_processes_callback_(fit::ok(CreateExceptionList()));
      watch_processes_callback_ = {};
      reply_watch_processes_ = false;
    }
  }

  std::vector<ProcessExceptionMetadata> CreateExceptionList() {
    std::vector<ProcessExceptionMetadata> processes;
    processes.reserve(processes_.size());
    for (auto& [process_koid, metadata] : processes_) {
      ProcessExceptionMetadata new_metadata = {};
      new_metadata.set_info(metadata.info());
      processes.push_back(std::move(new_metadata));
    }

    return processes;
  }

  // Not used for now.
  void GetFilters(GetFiltersCallback) override {}
  void AppendFilters(std::vector<std::string> filters, AppendFiltersCallback) override {}
  void RemoveFilters(std::vector<std::string> filters, RemoveFiltersCallback) override {}

  const std::map<zx_koid_t, ProcessExceptionMetadata>& processes() const { return processes_; }

  // Boilerplate needed for getting a FIDL binding to work in unit tests.
  fidl::InterfaceRequestHandler<ProcessLimbo> GetHandler() { return bindings_.GetHandler(this); }

  void set_is_active(bool is_active) { is_active_ = is_active; }

  void set_reply_active(bool reply) { reply_active_ = reply; }
  void set_reply_watch_processes(bool reply) { reply_watch_processes_ = reply; }

  bool has_watch_processes_callback() const { return !!watch_processes_callback_; }

  int watch_count() const { return watch_count_.load(); }

 private:
  std::map<zx_koid_t, ProcessExceptionMetadata> processes_;
  fidl::BindingSet<ProcessLimbo> bindings_;

  bool is_active_ = true;

  bool reply_active_ = true;

  bool reply_watch_processes_ = true;
  ProcessLimbo::WatchProcessesWaitingOnExceptionCallback watch_processes_callback_;

  std::atomic<int> watch_count_ = 0;
};

std::pair<const MockProcessObject*, const MockThreadObject*> GetProcessThread(
    const MockObjectProvider& object_provider, const std::string& process_name,
    const std::string& thread_name) {
  const MockProcessObject* process = object_provider.ProcessByName(process_name);
  FXL_DCHECK(process);
  const MockThreadObject* thread = process->GetThread(thread_name);
  FXL_DCHECK(thread);

  return {process, thread};
}

void RunUntil(async::Loop* loop, fit::function<bool()> condition,
              zx::duration step = zx::msec(10)) {
  while (!condition()) {
    loop->Run(zx::deadline_after(step));
  }
}

// Tests -------------------------------------------------------------------------------------------

TEST(LimboProvider, WatchProcessesOnException) {
  // Set the process limbo.
  auto object_provider = CreateDefaultMockObjectProvider();
  auto [process1, thread1] = GetProcessThread(*object_provider, "root-p2", "initial-thread");

  constexpr ExceptionType exception1 = ExceptionType::FATAL_PAGE_FAULT;
  auto [process2, thread2] = GetProcessThread(*object_provider, "job121-p2", "third-thread");
  constexpr ExceptionType exception2 = ExceptionType::UNALIGNED_ACCESS;

  StubProcessLimbo process_limbo;
  process_limbo.AppendException(*process1, *thread1, exception1);
  process_limbo.AppendException(*process2, *thread2, exception2);

  // Setup the async loop to respond to the async call.
  async::Loop remote_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(remote_loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_ZX_EQ(remote_loop.StartThread("process-limbo-thread"), ZX_OK);

  async::Loop local_loop(&kAsyncLoopConfigAttachToCurrentThread);
  LimboProvider limbo_provider(services.service_directory());
  ASSERT_ZX_EQ(limbo_provider.Init(), ZX_OK);
  ASSERT_TRUE(limbo_provider.Valid());

  process_limbo.set_reply_active(false);

  local_loop.RunUntilIdle();

  /* std::vector<ProcessExceptionMetadata> processes; */
  auto& processes = limbo_provider.Limbo();
  ASSERT_EQ(processes.size(), 2u);

  auto it = processes.begin();
  ASSERT_TRUE(it->second.has_info());
  EXPECT_EQ(it->second.info().process_koid, process1->koid);
  EXPECT_EQ(it->second.info().thread_koid, thread1->koid);
  EXPECT_EQ(it->second.info().type, exception1);

  it++;
  ASSERT_TRUE(it->second.has_info());
  EXPECT_EQ(it->second.info().process_koid, process2->koid);
  EXPECT_EQ(it->second.info().thread_koid, thread2->koid);
  EXPECT_EQ(it->second.info().type, exception2);
}

TEST(LimboProvider, RetriveException) {
  // Set the process limbo.
  auto object_provider = CreateDefaultMockObjectProvider();
  auto [process1, thread1] = GetProcessThread(*object_provider, "root-p2", "initial-thread");
  constexpr ExceptionType exception1 = ExceptionType::FATAL_PAGE_FAULT;
  auto [process2, thread2] = GetProcessThread(*object_provider, "job121-p2", "third-thread");
  constexpr ExceptionType exception2 = ExceptionType::UNALIGNED_ACCESS;

  StubProcessLimbo process_limbo;
  process_limbo.AppendException(*process1, *thread1, exception1);
  process_limbo.AppendException(*process2, *thread2, exception2);

  // Setup the async loop to respond to the async call.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_ZX_EQ(loop.StartThread("process-limbo-thread"), ZX_OK);

  LimboProvider limbo_provider(services.service_directory());

  // Some random koid should fail.
  fuchsia::exception::ProcessException exception;
  ASSERT_ZX_EQ(limbo_provider.RetrieveException(-1, &exception), ZX_ERR_NOT_FOUND);

  // Getting a valid one should work.
  ASSERT_ZX_EQ(limbo_provider.RetrieveException(process1->koid, &exception), ZX_OK);

  // We can only check the info in this test.
  EXPECT_EQ(exception.info().process_koid, process1->koid);
  EXPECT_EQ(exception.info().thread_koid, thread1->koid);
  EXPECT_EQ(exception.info().type, exception1);
}

TEST(LimboProvider, ReleaseProcess) {
  // Set the process limbo.
  auto object_provider = CreateDefaultMockObjectProvider();
  auto [process1, thread1] = GetProcessThread(*object_provider, "root-p2", "initial-thread");
  constexpr ExceptionType exception1 = ExceptionType::FATAL_PAGE_FAULT;
  auto [process2, thread2] = GetProcessThread(*object_provider, "job121-p2", "third-thread");
  constexpr ExceptionType exception2 = ExceptionType::UNALIGNED_ACCESS;

  StubProcessLimbo process_limbo;
  process_limbo.AppendException(*process1, *thread1, exception1);
  process_limbo.AppendException(*process2, *thread2, exception2);

  // Setup the async loop to respond to the async call.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_ZX_EQ(loop.StartThread("process-limbo-thread"), ZX_OK);

  LimboProvider limbo_provider(services.service_directory());

  // Some random koid should fail.
  fuchsia::exception::ProcessException exception;
  ASSERT_ZX_EQ(limbo_provider.ReleaseProcess(-1), ZX_ERR_NOT_FOUND);

  // Getting a valid one should work.
  ASSERT_ZX_EQ(limbo_provider.ReleaseProcess(process1->koid), ZX_OK);

  // There should only be one process left on limbo.
  ASSERT_EQ(process_limbo.processes().size(), 1u);
  auto it = process_limbo.processes().begin();
  EXPECT_EQ(it++->first, process2->koid);
}

TEST(LimboProvider, WatchProcessesCallback) {
  // Set the process limbo.
  auto object_provider = CreateDefaultMockObjectProvider();

  auto [process1, thread1] = GetProcessThread(*object_provider, "root-p2", "initial-thread");
  constexpr ExceptionType exception1 = ExceptionType::FATAL_PAGE_FAULT;

  auto [process2, thread2] = GetProcessThread(*object_provider, "job121-p2", "third-thread");
  constexpr ExceptionType exception2 = ExceptionType::UNALIGNED_ACCESS;

  StubProcessLimbo process_limbo;
  process_limbo.AppendException(*process1, *thread1, exception1);

  // Setup the async loop to respond to the async call.
  async::Loop remote_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(remote_loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_ZX_EQ(remote_loop.StartThread("process-limbo-thread"), ZX_OK);

  async::Loop local_loop(&kAsyncLoopConfigAttachToCurrentThread);
  LimboProvider limbo_provider(services.service_directory());
  ASSERT_ZX_EQ(limbo_provider.Init(), ZX_OK);
  ASSERT_TRUE(limbo_provider.Valid());

  local_loop.RunUntilIdle();

  RunUntil(&local_loop,
           [&process_limbo]() { return process_limbo.has_watch_processes_callback(); });

  {
    // There should be one exception on limbo.
    auto& limbo = limbo_provider.Limbo();
    ASSERT_EQ(limbo.size(), 1u);
    auto it = limbo.find(process1->koid);
    ASSERT_NE(it, limbo.end());
    EXPECT_EQ(it->second.info().process_koid, process1->koid);
    EXPECT_EQ(it->second.info().thread_koid, thread1->koid);
    EXPECT_EQ(it->second.info().type, exception1);
  }

  // Set the callback.

  bool called = false;
  std::vector<ProcessExceptionMetadata> exceptions;
  limbo_provider.set_on_enter_limbo(
      [&called, &exceptions](std::vector<ProcessExceptionMetadata> new_exceptions) {
        called = true;
        exceptions = std::move(new_exceptions);
      });

  // The event should've not been signaled.
  /* ASSERT_ZX_EQ(called.wait_one(ZX_USER_SIGNAL_0, zx::time(0), nullptr), ZX_ERR_TIMED_OUT); */
  ASSERT_FALSE(called);

  // We post an exception on the limbo's loop.
  {
    zx::event exception_posted;
    ASSERT_ZX_EQ(zx::event::create(0, &exception_posted), ZX_OK);
    async::PostTask(remote_loop.dispatcher(),
                    [p = process2, t = thread2, &process_limbo, &exception_posted]() {
                      // Add the new exception.
                      process_limbo.AppendException(*p, *t, exception2);
                      exception_posted.signal(0, ZX_USER_SIGNAL_0);
                    });

    // Wait until it was posted.
    ASSERT_ZX_EQ(exception_posted.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr), ZX_OK);
  }

  // Process the callback.
  RunUntil(&local_loop, [&called]() { return called; });

  // Should've called the callback.
  {
    ASSERT_TRUE(called);
    ASSERT_EQ(exceptions.size(), 1u);
    EXPECT_EQ(exceptions[0].info().process_koid, process2->koid);
    EXPECT_EQ(exceptions[0].info().thread_koid, thread2->koid);
    EXPECT_EQ(exceptions[0].info().type, exception2);

    // The limbo should be updated.
    auto& limbo = limbo_provider.Limbo();

    ASSERT_EQ(limbo.size(), 2u);

    auto it = limbo.find(process1->koid);
    ASSERT_NE(it, limbo.end());
    EXPECT_EQ(it->second.info().process_koid, process1->koid);
    EXPECT_EQ(it->second.info().thread_koid, thread1->koid);
    EXPECT_EQ(it->second.info().type, exception1);

    it = limbo.find(process2->koid);
    ASSERT_NE(it, limbo.end());
    EXPECT_EQ(it->second.info().process_koid, process2->koid);
    EXPECT_EQ(it->second.info().thread_koid, thread2->koid);
    EXPECT_EQ(it->second.info().type, exception2);
  }

  // Releasing an exception should also call the enter limbo callback.
  called = false;
  exceptions.clear();

  {
    zx::event release_event;
    ASSERT_ZX_EQ(zx::event::create(0, &release_event), ZX_OK);
    async::PostTask(remote_loop.dispatcher(),
                    [process_koid = process2->koid, &process_limbo, &release_event]() {
                      // Add the new exception.
                      process_limbo.ReleaseProcess(process_koid, [](auto a) {});
                      release_event.signal(0, ZX_USER_SIGNAL_0);
                    });

    // Wait until it was posted.
    ASSERT_ZX_EQ(release_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr), ZX_OK);
  }

  // The enter limbo callback should not have been called.
  ASSERT_FALSE(called);

  // We wait until the limbo have had a time to issue the other watch, thus having processes the
  // release callback.
  RunUntil(&local_loop, [&process_limbo]() { return process_limbo.watch_count() == 4; });

  // We should've received the call.
  // Should've called the callback.
  {
    // The limbo should be updated.
    auto& limbo = limbo_provider.Limbo();
    ASSERT_EQ(limbo.size(), 1u);

    auto it = limbo.find(process1->koid);
    ASSERT_NE(it, limbo.end());
    EXPECT_EQ(it->second.info().process_koid, process1->koid);
    EXPECT_EQ(it->second.info().thread_koid, thread1->koid);
    EXPECT_EQ(it->second.info().type, exception1);
  }
}

}  // namespace
}  // namespace debug_agent
