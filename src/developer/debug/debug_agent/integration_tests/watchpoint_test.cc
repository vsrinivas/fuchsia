// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"
#include "src/developer/debug/debug_agent/integration_tests/so_wrapper.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/zircon_system_interface.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

using namespace debug_ipc;

namespace {

constexpr int kInvalidReturnCode = 0xdeadbeef;

// Receives the notification from the DebugAgent.
// The implementation is at the end of the file.
class WatchpointStreamBackend : public LocalStreamBackend {
 public:
  WatchpointStreamBackend(debug::MessageLoop* loop) : loop_(loop) {}

  void set_remote_api(RemoteAPI* remote_api) { remote_api_ = remote_api; }

  // API -----------------------------------------------------------------------

  void ResumeAllThreads();
  void ResumeAllThreadsAndRunLoop();

  // Notification Interception -------------------------------------------------

  void HandleNotifyModules(NotifyModules) override;
  void HandleNotifyException(NotifyException) override;
  void HandleNotifyProcessExiting(NotifyProcessExiting) override;
  void HandleNotifyThreadStarting(NotifyThreadStarting) override;

  // Getters -------------------------------------------------------------------

  debug::MessageLoop* loop() const { return loop_; }
  uint64_t so_test_base_addr() const { return so_test_base_addr_; }

  zx_koid_t process_koid() const { return process_koid_; }
  zx_koid_t thread_koid() const { return thread_koid_; }
  int return_code() const { return return_code_; }

  const auto& exceptions() const { return exceptions_; }

 private:
  // Each trapped notification will forward the decision whether to quit the
  // loop to this call according to the TestStage enum.
  void ShouldQuitLoop();

  enum class TestStage {
    kWaitingForThread,
    kWaitingForModules,
    kWaitingForException,
    kWaitingForExit,
    kDone,
  };

  TestStage test_stage_ = TestStage::kWaitingForThread;

  debug::MessageLoop* loop_ = nullptr;
  RemoteAPI* remote_api_ = nullptr;

  uint64_t so_test_base_addr_ = 0;

  zx_koid_t process_koid_ = 0;
  zx_koid_t thread_koid_ = 0;

  std::vector<NotifyException> exceptions_;
  int return_code_ = kInvalidReturnCode;
};

constexpr uint32_t kWatchpointId = 0x1234;

std::pair<LaunchRequest, LaunchReply> GetLaunchRequest(const WatchpointStreamBackend& backend,
                                                       std::string exe);

std::pair<AddOrChangeBreakpointRequest, AddOrChangeBreakpointReply> GetWatchpointRequest(
    const WatchpointStreamBackend& backend, uint64_t address);

#if defined(__x86_64__)
TEST(Watchpoint, DISABLED_DefaultCase) {
// Arm64 implementation is not done yet.
#elif defined(__aarch64__)
TEST(Watchpoint, DISABLED_DefaultCase) {
#endif
  // Activate this is the test is giving you trouble.
  // debug::SetDebugMode(true);

  static constexpr const char kTestSo[] = "debug_agent_test_so.so";
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  uint64_t variable_offset = so_wrapper.GetSymbolOffset(kTestSo, "gWatchpointVariable");
  ASSERT_NE(variable_offset, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();

    WatchpointStreamBackend backend(loop);
    DebugAgent agent(std::make_unique<ZirconSystemInterface>());
    RemoteAPI* remote_api = &agent;

    agent.Connect(&backend.stream());
    backend.set_remote_api(remote_api);

    static constexpr const char kExecutable[] = "/pkg/bin/watchpoint_test_exe";
    auto [lnch_request, lnch_reply] = GetLaunchRequest(backend, kExecutable);
    remote_api->OnLaunch(lnch_request, &lnch_reply);
    ASSERT_TRUE(lnch_reply.status.ok());

    backend.ResumeAllThreadsAndRunLoop();

    // The first thread should've started.
    ASSERT_NE(backend.process_koid(), 0u);
    ASSERT_NE(backend.thread_koid(), 0u);

    // We should have the correct module by now.
    ASSERT_NE(backend.so_test_base_addr(), 0u);
    uint64_t address = backend.so_test_base_addr() + variable_offset;

    DEBUG_LOG(Test) << std::hex << "Base: 0x" << backend.so_test_base_addr() << ", Offset: 0x"
                    << variable_offset << ", Actual Address: 0x" << address;

    auto [wp_request, wp_reply] = GetWatchpointRequest(backend, address);
    remote_api->OnAddOrChangeBreakpoint(wp_request, &wp_reply);
    ASSERT_TRUE(wp_reply.status.ok());

    backend.ResumeAllThreadsAndRunLoop();

    // We should've gotten an exception.
    auto& exceptions = backend.exceptions();
    ASSERT_EQ(exceptions.size(), 1u);

    auto& exception = exceptions[0];
    EXPECT_EQ(exception.type, ExceptionType::kWatchpoint) << ExceptionTypeToString(exception.type);
    EXPECT_EQ(exception.thread.id.process, backend.process_koid());
    EXPECT_EQ(exception.thread.id.thread, backend.thread_koid());

    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);
    auto& wp = exception.hit_breakpoints[0];
    EXPECT_EQ(wp.id, kWatchpointId);
    EXPECT_EQ(wp.hit_count, 1u);
    EXPECT_EQ(wp.should_delete, true);

    backend.ResumeAllThreadsAndRunLoop();

    // The process should've exited correctly.
    EXPECT_EQ(backend.return_code(), 0);
  }
}

// Helpers ---------------------------------------------------------------------

std::pair<LaunchRequest, LaunchReply> GetLaunchRequest(const WatchpointStreamBackend& backend,
                                                       std::string exe) {
  LaunchRequest launch_request = {};
  launch_request.argv = {exe};
  launch_request.inferior_type = InferiorType::kBinary;
  return {launch_request, {}};
}

std::pair<AddOrChangeBreakpointRequest, AddOrChangeBreakpointReply> GetWatchpointRequest(
    const WatchpointStreamBackend& backend, uint64_t address) {
  // We add a breakpoint in that address.
  debug_ipc::ProcessBreakpointSettings location = {};
  location.id = {.process = backend.process_koid(), .thread = backend.thread_koid()};
  location.address_range = {address, address};

  debug_ipc::AddOrChangeBreakpointRequest watchpoint_request = {};
  watchpoint_request.breakpoint.id = kWatchpointId;
  watchpoint_request.breakpoint.type = BreakpointType::kWrite;
  watchpoint_request.breakpoint.one_shot = true;
  watchpoint_request.breakpoint.locations.push_back(location);

  return {watchpoint_request, {}};
}

// WatchpointStreamBackend Implementation --------------------------------------

void WatchpointStreamBackend::ResumeAllThreadsAndRunLoop() {
  ResumeAllThreads();
  loop()->Run();
}

void WatchpointStreamBackend::ResumeAllThreads() {
  debug_ipc::ResumeRequest resume_request;
  resume_request.ids.push_back({.process = process_koid(), .thread = 0});
  debug_ipc::ResumeReply resume_reply;
  remote_api_->OnResume(resume_request, &resume_reply);
}

// Searches the loaded modules for specific one.
void WatchpointStreamBackend::HandleNotifyModules(NotifyModules modules) {
  for (auto& module : modules.modules) {
    DEBUG_LOG(Test) << "Received module " << module.name;
    if (module.name == "libdebug_agent_test_so.so") {
      so_test_base_addr_ = module.base;
      break;
    }
  }
  ShouldQuitLoop();
}

// Records the exception given from the debug agent.
void WatchpointStreamBackend::HandleNotifyException(NotifyException exception) {
  DEBUG_LOG(Test) << "Received " << ExceptionTypeToString(exception.type)
                  << " on Thread: " << exception.thread.id.thread;
  exceptions_.push_back(std::move(exception));
  ShouldQuitLoop();
}

void WatchpointStreamBackend::HandleNotifyThreadStarting(NotifyThreadStarting thread) {
  process_koid_ = thread.record.id.process;
  thread_koid_ = thread.record.id.thread;
  ShouldQuitLoop();
}

void WatchpointStreamBackend::HandleNotifyProcessExiting(NotifyProcessExiting process) {
  DEBUG_LOG(Test) << "Process " << process.process_koid
                  << " exiting with return code: " << process.return_code;
  FX_DCHECK(process.process_koid == process_koid_);
  return_code_ = process.return_code;
  ShouldQuitLoop();
}

void WatchpointStreamBackend::ShouldQuitLoop() {
  if (test_stage_ == TestStage::kWaitingForThread) {
    if (process_koid_ != 0u && thread_koid_ != 0u) {
      test_stage_ = TestStage::kWaitingForModules;
      DEBUG_LOG(Test) << "Stage changed to WAITING FOR MODULES.";

      // In this case we resume the thread.
      ResumeAllThreads();
      return;
    }
  } else if (test_stage_ == TestStage::kWaitingForModules) {
    if (so_test_base_addr_ != 0u) {
      test_stage_ = TestStage::kWaitingForException;
      DEBUG_LOG(Test) << "State changed to WAITING FOR EXCEPTION.";
      loop_->QuitNow();
      return;
    }
  } else if (test_stage_ == TestStage::kWaitingForException) {
    if (exceptions_.size() == 1u) {
      test_stage_ = TestStage::kWaitingForExit;
      DEBUG_LOG(Test) << "State changed to WAITING FOR EXIT.";
      loop_->QuitNow();
      return;
    }
  } else if (test_stage_ == TestStage::kWaitingForExit) {
    if (return_code_ != kInvalidReturnCode) {
      test_stage_ = TestStage::kDone;
      DEBUG_LOG(Test) << "State changed to DONE.";
      loop_->QuitNow();
      return;
    }
  }

  FX_NOTREACHED() << "Invalid test state.";
}

}  // namespace

}  // namespace debug_agent
