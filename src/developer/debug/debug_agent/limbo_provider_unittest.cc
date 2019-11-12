// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/limbo_provider.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/status.h>

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
  void WatchActive(WatchActiveCallback callback) override {
    if (!reply_active_)
      return;

    callback(is_active_);
  }

  void WatchProcessesWaitingOnException(
      ProcessLimbo::WatchProcessesWaitingOnExceptionCallback callback) override {
    if (!reply_watch_processes_)
      return;

    std::vector<ProcessExceptionMetadata> processes;
    processes.reserve(processes_.size());
    for (auto& [process_koid, metadata] : processes_) {
      processes.push_back(std::move(metadata));
    }

    processes_.clear();
    callback(fit::ok(std::move(processes)));
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

    it = processes_.erase(it);
    return cb(fit::ok());
  }

  void AppendException(const MockProcessObject& process, const MockThreadObject& thread,
                       ExceptionType exception_type) {
    ExceptionInfo info = {};
    info.process_koid = process.koid;
    info.thread_koid = thread.koid;
    info.type = exception_type;

    ProcessExceptionMetadata metadata = {};
    metadata.set_info(std::move(info));
    // Sadly we cannot send bad handles over a channel, so we cannot actually send the "invented"
    // handles for this test. Setting the info is enough though.
    // metadata.set_process(process.GetHandle());
    // metadata.set_thread(thread.GetHandle());

    processes_[info.process_koid] = std::move(metadata);
  }

  const std::map<zx_koid_t, ProcessExceptionMetadata>& processes() const { return processes_; }

  // Boilerplate needed for getting a FIDL binding to work in unit tests.
  fidl::InterfaceRequestHandler<ProcessLimbo> GetHandler() { return bindings_.GetHandler(this); }

  void set_is_active(bool is_active) { is_active_ = is_active; }

  void set_reply_active(bool reply) { reply_active_ = reply; }
  void set_reply_watch_processes(bool reply) { reply_watch_processes_ = reply; }

 private:
  std::map<zx_koid_t, ProcessExceptionMetadata> processes_;
  fidl::BindingSet<ProcessLimbo> bindings_;

  bool is_active_ = true;

  bool reply_active_ = true;
  bool reply_watch_processes_ = true;
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
  process_limbo.set_reply_watch_processes(false);

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

}  // namespace
}  // namespace debug_agent
