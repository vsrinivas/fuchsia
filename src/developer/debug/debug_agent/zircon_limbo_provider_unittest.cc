// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_limbo_provider.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/test_utils.h"

using namespace ::fuchsia::exception;

namespace debug_agent {
namespace {

class StubProcessLimbo : public fuchsia::exception::ProcessLimbo {
 public:
  void SetActive(bool active, SetActiveCallback cb) override {
    FX_NOTREACHED() << "Not needed for tests.";
  }

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

    callback(fpromise::ok(CreateExceptionList()));
    reply_watch_processes_ = false;
  }

  void RetrieveException(zx_koid_t process_koid,
                         ProcessLimbo::RetrieveExceptionCallback callback) override {
    auto it = processes_.find(process_koid);
    if (it == processes_.end())
      return callback(fpromise::error(ZX_ERR_NOT_FOUND));

    // We cannot set any fake handles, as they will fail on the channel write.
    ProcessException exception;
    exception.set_info(it->second.info());

    processes_.erase(it);
    callback(fpromise::ok(std::move(exception)));
  }

  void ReleaseProcess(zx_koid_t process_koid, ProcessLimbo::ReleaseProcessCallback cb) override {
    auto it = processes_.find(process_koid);
    if (it == processes_.end())
      return cb(fpromise::error(ZX_ERR_NOT_FOUND));

    processes_.erase(it);
    cb(fpromise::ok());

    if (reply_watch_processes_ && watch_processes_callback_) {
      watch_processes_callback_(fpromise::ok(CreateExceptionList()));
      reply_watch_processes_ = false;
      return;
    }
  }

  void AppendException(zx_koid_t process_koid, zx_koid_t thread_koid,
                       ExceptionType exception_type) {
    ExceptionInfo info = {};
    info.process_koid = process_koid;
    info.thread_koid = thread_koid;
    info.type = exception_type;

    // Track the metadata in the limbo.
    ProcessExceptionMetadata metadata = {};
    metadata.set_info(std::move(info));

    // Sadly we cannot send bad handles over a channel, so we cannot actually send the "invented"
    // handles for this test. Setting the info is enough though.
    // metadata.set_process(...);
    // metadata.set_thread(...);
    processes_[info.process_koid] = std::move(metadata);

    // If there is a callback, only send the new exceptions over.
    if (watch_processes_callback_) {
      watch_processes_callback_(fpromise::ok(CreateExceptionList()));
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

void RunUntil(async::Loop* loop, fit::function<bool()> condition,
              zx::duration step = zx::msec(10)) {
  while (!condition()) {
    loop->Run(zx::deadline_after(step));
  }
}

}  // namespace

// Tests -------------------------------------------------------------------------------------------

TEST(ZirconLimboProvider, WatchProcessesOnException) {
  StubProcessLimbo process_limbo;

  constexpr zx_koid_t kProc1Koid = 100;
  constexpr zx_koid_t kThread1Koid = 101;
  process_limbo.AppendException(kProc1Koid, kThread1Koid, ExceptionType::FATAL_PAGE_FAULT);

  constexpr zx_koid_t kProc2Koid = 102;
  constexpr zx_koid_t kThread2Koid = 103;
  process_limbo.AppendException(kProc2Koid, kThread2Koid, ExceptionType::UNALIGNED_ACCESS);

  // Setup the async loop to respond to the async call.
  async::Loop remote_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(remote_loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_ZX_EQ(remote_loop.StartThread("process-limbo-thread"), ZX_OK);

  async::Loop local_loop(&kAsyncLoopConfigAttachToCurrentThread);
  ZirconLimboProvider limbo_provider(services.service_directory());
  ASSERT_TRUE(limbo_provider.Valid());

  process_limbo.set_reply_active(false);

  local_loop.RunUntilIdle();

  // Validate that both exceptions came through. The handles aren't real so the values will not
  // be useful, but we can verify that two come out the other end.
  const auto& processes = limbo_provider.GetLimboRecords();
  ASSERT_EQ(processes.size(), 2u);
  EXPECT_NE(processes.find(kProc1Koid), processes.end());
  EXPECT_NE(processes.find(kProc2Koid), processes.end());
}

TEST(ZirconLimboProvider, WatchProcessesCallback) {
  constexpr zx_koid_t kProc1Koid = 100;
  constexpr zx_koid_t kThread1Koid = 101;
  constexpr auto kException1Type = ExceptionType::FATAL_PAGE_FAULT;
  StubProcessLimbo process_limbo;
  process_limbo.AppendException(kProc1Koid, kThread1Koid, kException1Type);

  // These will be appended later.
  constexpr zx_koid_t kProc2Koid = 102;
  constexpr zx_koid_t kThread2Koid = 103;
  constexpr auto kException2Type = ExceptionType::UNALIGNED_ACCESS;

  // Setup the async loop to respond to the async call.
  async::Loop remote_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(remote_loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_ZX_EQ(remote_loop.StartThread("process-limbo-thread"), ZX_OK);

  async::Loop local_loop(&kAsyncLoopConfigAttachToCurrentThread);
  ZirconLimboProvider limbo_provider(services.service_directory());
  ASSERT_TRUE(limbo_provider.Valid());

  local_loop.RunUntilIdle();

  RunUntil(&local_loop,
           [&process_limbo]() { return process_limbo.has_watch_processes_callback(); });

  {
    // There should be one exception in limbo.
    const auto& limbo = limbo_provider.GetLimboRecords();
    ASSERT_EQ(limbo.size(), 1u);
    EXPECT_NE(limbo.find(kProc1Koid), limbo.end());
  }

  // Set the callback.
  int called_count = 0;
  limbo_provider.set_on_enter_limbo(
      [&called_count](const ZirconLimboProvider::Record& record) { called_count++; });

  // The event should've not been signaled.
  ASSERT_EQ(called_count, 0);

  // We post an exception on the limbo's loop.
  {
    zx::event exception_posted;
    ASSERT_ZX_EQ(zx::event::create(0, &exception_posted), ZX_OK);
    async::PostTask(remote_loop.dispatcher(), [&process_limbo, &exception_posted]() {
      // Add the new exception.
      process_limbo.AppendException(kProc2Koid, kThread2Koid, kException2Type);
      exception_posted.signal(0, ZX_USER_SIGNAL_0);
    });

    // Wait until it was posted.
    ASSERT_ZX_EQ(exception_posted.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr), ZX_OK);
  }

  // Process the callback.
  RunUntil(&local_loop, [&called_count]() { return called_count > 0; });

  // Should've called the callback.
  {
    ASSERT_EQ(called_count, 1);
    const auto& records = limbo_provider.GetLimboRecords();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(records.find(kProc1Koid), records.end());
    EXPECT_NE(records.find(kProc2Koid), records.end());
  }

  // Releasing an exception should also call the enter limbo callback.
  called_count = 0;

  {
    zx::event release_event;
    ASSERT_ZX_EQ(zx::event::create(0, &release_event), ZX_OK);
    async::PostTask(remote_loop.dispatcher(), [&process_limbo, &release_event]() {
      // Add the new exception.
      process_limbo.ReleaseProcess(kProc2Koid, [](auto a) {});
      release_event.signal(0, ZX_USER_SIGNAL_0);
    });

    // Wait until it was posted.
    ASSERT_ZX_EQ(release_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr), ZX_OK);
  }

  // The enter limbo callback should not have been called.
  ASSERT_EQ(called_count, 0);

  // We wait until the limbo have had a time to issue the other watch, thus having processes the
  // release callback.
  RunUntil(&local_loop, [&process_limbo]() { return process_limbo.watch_count() == 4; });

  // The limbo should be updated.
  {
    const auto& records = limbo_provider.GetLimboRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_NE(records.find(kProc1Koid), records.end());
  }
}

}  // namespace debug_agent
