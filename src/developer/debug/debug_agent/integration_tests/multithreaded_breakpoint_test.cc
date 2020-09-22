// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"
#include "src/developer/debug/debug_agent/integration_tests/so_wrapper.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/zircon_system_interface.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

using namespace debug_ipc;

// This tests verifies that in a multithreaded program the debugger is able to
// setup a breakpoint that will only affect a single thread and let the others
// run without stopping in it.

namespace {

// Receives the notification from the DebugAgent.
// The implementation is at the end of the file.
class BreakpointStreamBackend : public LocalStreamBackend {
 public:
  // In what part of the test we currently are.
  // This will determine when we quit the loop to let the test verify state.
  enum class TestStage {
    // Waits for the first thread start and modules.
    kWaitingForThreadToStart,

    // Waits for the other thread starting notifications.
    kCreatingOtherThreads,

    // Waits for the one thread to hit the breakpoint and all other threads
    // to exit.
    kExpectingBreakpointAndTerminations,

    // Waiting for the last thread to exit.
    kWaitingForFinalExit,

    // Waiting for the process to exit.
    kDone,

    kInvalid,
  };

  BreakpointStreamBackend(MessageLoop* loop, size_t thread_count)
      : loop_(loop), thread_count_(thread_count) {}

  void set_remote_api(RemoteAPI* remote_api) { remote_api_ = remote_api; }

  // API -----------------------------------------------------------------------

  // Will send a resume notification to all threads and run the loop.
  void ResumeAllThreadsAndRunLoop();

  // The messages we're interested in handling ---------------------------------

  // Searches the loaded modules for specific one.
  void HandleNotifyModules(NotifyModules) override;
  void HandleNotifyProcessExiting(NotifyProcessExiting) override;
  void HandleNotifyThreadStarting(NotifyThread) override;
  void HandleNotifyThreadExiting(NotifyThread) override;
  void HandleNotifyException(NotifyException) override;

  // Getters -------------------------------------------------------------------

  MessageLoop* loop() const { return loop_; }

  uint64_t so_test_base_addr() const { return so_test_base_addr_; }

  zx_koid_t process_koid() const { return process_koid_; }
  bool process_exited() const { return process_exited_; }
  int64_t return_code() const { return return_code_; }

  size_t thread_count() const { return thread_count_; }
  const auto& thread_koids() const { return thread_koids_; }
  const auto& thread_starts() const { return thread_starts_; }
  const auto& thread_excp() const { return thread_excp_; }
  const auto& thread_exits() const { return thread_exits_; }

 private:
  // Every exception should ask whether it should stop the loop and let the test
  // verify if what happened is correct. This function holds the "script" that
  // the tests follows in order to work properly.
  void ShouldQuitLoop();

  // Similar to ResumeAllThreadsAndRunLoop, but doesn't run the loop.
  void ResumeAllThreads();

  MessageLoop* loop_ = nullptr;
  RemoteAPI* remote_api_ = nullptr;

  uint64_t so_test_base_addr_ = 0;

  zx_koid_t process_koid_ = 0;
  bool process_exited_ = false;
  int64_t return_code_ = 0;

  size_t thread_count_ = 0;
  std::vector<zx_koid_t> thread_koids_;
  std::vector<NotifyThread> thread_starts_;
  std::vector<NotifyException> thread_excp_;
  std::vector<NotifyThread> thread_exits_;

  bool initial_thread_check_passed_ = false;
  bool got_modules_check_passed_ = false;
  bool process_finished_check_passed_ = false;

  TestStage test_stage_ = TestStage::kWaitingForThreadToStart;
};

std::pair<LaunchRequest, LaunchReply> GetLaunchRequest(const BreakpointStreamBackend& backend,
                                                       std::string exe) {
  LaunchRequest launch_request = {};
  launch_request.argv = {exe, fxl::StringPrintf("%lu", backend.thread_count())};
  launch_request.inferior_type = InferiorType::kBinary;
  return {launch_request, {}};
}

constexpr uint32_t kBreakpointId = 1234u;

std::pair<AddOrChangeBreakpointRequest, AddOrChangeBreakpointReply> GetBreakpointRequest(
    zx_koid_t process_koid, zx_koid_t thread_koid, uint64_t address) {
  // We add a breakpoint in that address.
  debug_ipc::ProcessBreakpointSettings location = {};
  location.process_koid = process_koid;
  location.thread_koid = thread_koid;
  location.address = address;

  debug_ipc::AddOrChangeBreakpointRequest breakpoint_request = {};
  breakpoint_request.breakpoint.id = kBreakpointId;
  breakpoint_request.breakpoint.locations.push_back(location);

  DEBUG_LOG(Test) << "Setting breakpoint for [P: " << process_koid << ", T: " << thread_koid
                  << "] on 0x" << std::hex << address;

  return {breakpoint_request, {}};
}

}  // namespace

#if defined(__x86_64__)
// TODO(fxbug.dev/6298): This is flaky on X64 for an unknown reason.
TEST(MultithreadedBreakpoint, DISABLED_SWBreakpoint) {
#elif defined(__aarch64__)
// TODO(fxbug.dev/6248): Arm64 has an instruction cache that makes a thread sometimes
//                hit a thread that has been removed, making this test flake.
//                This has to be fixed in zircon.
TEST(MultithreadedBreakpoint, DISABLED_SWBreakpoint) {
#endif
  // Uncomment these is the test is giving you trouble.
  // Only uncomment SetDebugMode if the test is giving you *real* trouble.
  // debug_ipc::SetDebugMode(true);
  // debug_ipc::SetLogCategories({LogCategory::kTest});

  // We attempt to load the pre-made .so.
  static constexpr const char kTestSo[] = "debug_agent_test_so.so";
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  uint64_t symbol_offset = so_wrapper.GetSymbolOffset(kTestSo, "MultithreadedFunctionToBreakOn");
  ASSERT_NE(symbol_offset, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();

    // The stream backend will intercept the calls from the debug agent.
    // Second arguments is the amount of threads to create.
    BreakpointStreamBackend backend(loop, 5);

    DebugAgent agent(std::make_unique<ZirconSystemInterface>());
    RemoteAPI* remote_api = &agent;

    agent.Connect(&backend.stream());
    backend.set_remote_api(remote_api);

    static constexpr const char kExecutable[] = "/pkg/bin/multithreaded_breakpoint_test_exe";
    auto [lnch_request, lnch_reply] = GetLaunchRequest(backend, kExecutable);
    remote_api->OnLaunch(lnch_request, &lnch_reply);
    ASSERT_EQ(lnch_reply.status, ZX_OK) << ZxStatusToString(lnch_reply.status);

    backend.ResumeAllThreadsAndRunLoop();

    // We should have the correct module by now.
    ASSERT_NE(backend.so_test_base_addr(), 0u);

    // We let the main thread spin up all the other threads.
    backend.ResumeAllThreadsAndRunLoop();

    // At this point all sub-threads should have started.
    ASSERT_EQ(backend.thread_starts().size(), backend.thread_count() + 1);

    // Set a breakpoint
    auto& thread_koids = backend.thread_koids();
    auto thread_koid = thread_koids[1];

    // We get the offset of the loaded function within the process space.
    uint64_t module_base = backend.so_test_base_addr();
    uint64_t module_function = module_base + symbol_offset;
    DEBUG_LOG(Test) << std::hex << "BASE: 0x" << module_base << ", OFFSET: 0x" << symbol_offset
                    << ", FINAL: 0x" << module_function;

    auto [brk_request, brk_reply] =
        GetBreakpointRequest(backend.process_koid(), thread_koid, module_function);
    remote_api->OnAddOrChangeBreakpoint(brk_request, &brk_reply);
    ASSERT_EQ(brk_reply.status, ZX_OK) << ZxStatusToString(brk_reply.status);

    backend.ResumeAllThreadsAndRunLoop();

    // At this point all threads should've exited except one in breakpoint and
    // the initial thread.
    auto& thread_exits = backend.thread_exits();
    ASSERT_EQ(thread_exits.size(), thread_koids.size() - 2);

    auto& thread_excp = backend.thread_excp();
    ASSERT_EQ(thread_excp.size(), 1u);
    auto& brk_notify = thread_excp.front();
    EXPECT_EQ(brk_notify.thread.thread_koid, thread_koid);
    EXPECT_EQ(brk_notify.type, debug_ipc::ExceptionType::kSoftwareBreakpoint);

    ASSERT_EQ(brk_notify.hit_breakpoints.size(), 1u);
    auto& hit_brk = brk_notify.hit_breakpoints.front();
    EXPECT_EQ(hit_brk.id, kBreakpointId);
    EXPECT_EQ(hit_brk.hit_count, 1u);
    EXPECT_EQ(hit_brk.should_delete, false);

    backend.ResumeAllThreadsAndRunLoop();

    // At this point all threads and processes should've exited.
    EXPECT_EQ(backend.thread_exits().size(), backend.thread_starts().size());
    ASSERT_TRUE(backend.process_exited());
    EXPECT_EQ(backend.return_code(), 0);
  }
}

// BreakpointStreamBackend Implementation --------------------------------------

void BreakpointStreamBackend::ResumeAllThreadsAndRunLoop() {
  ResumeAllThreads();
  loop()->Run();
}

void BreakpointStreamBackend::ResumeAllThreads() {
  debug_ipc::ResumeRequest resume_request;
  resume_request.process_koid = process_koid();
  debug_ipc::ResumeReply resume_reply;
  remote_api_->OnResume(resume_request, &resume_reply);
}

// Records the exception given from the debug agent.
void BreakpointStreamBackend::HandleNotifyException(NotifyException exception) {
  DEBUG_LOG(Test) << "Received " << ExceptionTypeToString(exception.type)
                  << " on Thread: " << exception.thread.thread_koid;
  thread_excp_.push_back(exception);
  ShouldQuitLoop();
}

// Searches the loaded modules for specific one.
void BreakpointStreamBackend::HandleNotifyModules(NotifyModules modules) {
  for (auto& module : modules.modules) {
    DEBUG_LOG(Test) << "Received module " << module.name;
    if (module.name == "libdebug_agent_test_so.so") {
      so_test_base_addr_ = module.base;
      break;
    }
  }
  ShouldQuitLoop();
}

void BreakpointStreamBackend::HandleNotifyProcessExiting(NotifyProcessExiting process) {
  DEBUG_LOG(Test) << "Process " << process.process_koid
                  << " exiting with return code: " << process.return_code;
  FX_DCHECK(process.process_koid == process_koid_);
  process_exited_ = true;
  return_code_ = process.return_code;
  ShouldQuitLoop();
}

void BreakpointStreamBackend::HandleNotifyThreadStarting(NotifyThread thread) {
  if (process_koid_ == 0) {
    process_koid_ = thread.record.process_koid;
    DEBUG_LOG(Test) << "Process starting: " << process_koid_;
  }
  DEBUG_LOG(Test) << "Thread starting: " << thread.record.thread_koid;
  thread_starts_.push_back(thread);
  thread_koids_.push_back(thread.record.thread_koid);
  ShouldQuitLoop();
}

void BreakpointStreamBackend::HandleNotifyThreadExiting(NotifyThread thread) {
  DEBUG_LOG(Test) << "Thread exiting: " << thread.record.thread_koid;
  thread_exits_.push_back(thread);
  ShouldQuitLoop();
}

void BreakpointStreamBackend::ShouldQuitLoop() {
  if (test_stage_ == TestStage::kWaitingForThreadToStart) {
    // The first thread started, we need to resume it.
    if (initial_thread_check_passed_ == 0 && thread_starts_.size() == 1u) {
      initial_thread_check_passed_ = true;
      ResumeAllThreads();
      return;
    }

    if (!got_modules_check_passed_ && so_test_base_addr_ != 0u) {
      got_modules_check_passed_ = true;
      loop()->QuitNow();
      test_stage_ = TestStage::kCreatingOtherThreads;
      DEBUG_LOG(Test) << "Stage change to CREATING OTHER THREADS";
      return;
    }

    FX_NOTREACHED() << "Didn't get thread start or modules.";
  }

  if (test_stage_ == TestStage::kCreatingOtherThreads) {
    if (thread_starts_.size() < thread_count_ + 1) {
      return;
    } else if (thread_starts_.size() == thread_count_ + 1) {
      // We received all the threads we expected for, quit the loop.
      loop()->QuitNow();
      test_stage_ = TestStage::kExpectingBreakpointAndTerminations;
      DEBUG_LOG(Test) << "Stage change to EXPECTING BREAKPOINT";
      return;
    }

    FX_NOTREACHED() << "Didn't get all the thread startups.";
  }

  if (test_stage_ == TestStage::kExpectingBreakpointAndTerminations) {
    // We should only get one breakpoint.
    if (thread_excp_.size() > 1u)
      FX_NOTREACHED() << "Got more than 1 exception.";

    // All subthreads should exit but one.
    if (thread_exits_.size() < thread_count_ - 1)
      return;

    if (thread_excp_.size() != 1u)
      FX_NOTREACHED() << "Should've gotten one breakpoint exception.";

    if (thread_exits_.size() != thread_count_ - 1)
      FX_NOTREACHED() << "All subthreads but one should've exited.";

    loop()->QuitNow();
    test_stage_ = TestStage::kWaitingForFinalExit;
    DEBUG_LOG(Test) << "Stage change to WAITING FOR FINAL EXIT.";

    return;
  }

  if (test_stage_ == TestStage::kWaitingForFinalExit) {
    // This is the breakpoint thread.
    if (thread_exits_.size() < thread_starts_.size())
      return;

    if (thread_exits_.size() == thread_starts_.size()) {
      test_stage_ = TestStage::kDone;
      DEBUG_LOG(Test) << "Stage change to DONE.";
      return;
    }

    FX_NOTREACHED() << "Unexpected thread exit.";
  }

  if (test_stage_ == TestStage::kDone) {
    if (!process_finished_check_passed_ && process_exited_) {
      process_finished_check_passed_ = true;
      loop()->QuitNow();
      test_stage_ = TestStage::kInvalid;
      return;
    }

    FX_NOTREACHED() << "Should've only received process exit notification.";
  }

  FX_NOTREACHED() << "Invalid stage.";
}

}  // namespace debug_agent
