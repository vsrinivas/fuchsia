// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"
#include "src/developer/debug/debug_agent/integration_tests/so_wrapper.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/zircon_system_interface.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

// This test is an integration test to verify that the debug agent is able to successfully set
// breakpoints to Zircon and get the correct responses. This particular test does the following
// script:
//
// 1. Load a pre-made .so (debug_agent_test_so) and search for a particular exported function. By
//    also getting the loaded base address of the .so, we can get the offset of the function within
//    the module.
//
// 2. Launch a process (through RemoteAPI::OnLaunch) control by the debug agent.
//
// 3. Get the module notication (NotifyModules message) for the process launched in (2). We look
//    over the modules for the same module (debug_agent_test_so) that was loaded by this newly
//    created process. With the base address of this module, we can use the offset calculated in (1)
//    and get the actual loaded address for the exported function within the process.
//
// 4. Set a breakpoint on that address and resume the process. The test program is written such that
//    it will call the searched symbol, so should hit the breakpoint.
//
// 5. Verify that we get a breakpoint exception on that address.
//
// 6. Success!

// The exported symbol we're going to put the breakpoint on.
const char* kExportedFunctionName = "InsertBreakpointFunction";
const char* kExportedFunctionName2 = "InsertBreakpointFunction2";

// The test .so we load in order to search the offset of the exported symbol
// within it.
const char* kTestSo = "debug_agent_test_so.so";

// The test executable the debug agent is going to launch. This is linked with |kTestSo|, meaning
// that the offset within that .so will be valid into the loaded module of this executable.
/* const char* kTestExecutableName = "breakpoint_test_exe"; */
const char* kTestExecutablePath = "/pkg/bin/breakpoint_test_exe";
const char* kModuleToSearch = "libdebug_agent_test_so.so";

class BreakpointStreamBackend : public LocalStreamBackend {
 public:
  BreakpointStreamBackend(debug::MessageLoop* loop) : loop_(loop) {}

  uint64_t so_test_base_addr() const { return so_test_base_addr_; }

  bool thread_started() const { return thread_started_; }
  bool thread_exited() const { return thread_exited_; }

  bool process_exited() const { return process_exited_; }

  zx_koid_t process_koid() const { return process_koid_; }
  zx_koid_t thread_koid() const { return thread_koid_; }

  const std::vector<debug_ipc::NotifyException>& exceptions() const { return exceptions_; }

  // The messages we're interested in handling ---------------------------------

  // Searches the loaded modules for specific one.
  void HandleNotifyModules(debug_ipc::NotifyModules modules) override {
    for (auto& module : modules.modules) {
      if (module.name == kModuleToSearch) {
        so_test_base_addr_ = module.base;
        break;
      }
    }
    loop_->QuitNow();
  }

  void HandleNotifyThreadStarting(debug_ipc::NotifyThreadStarting thread) override {
    ASSERT_FALSE(thread_started_);
    thread_started_ = true;
    loop_->QuitNow();
  }

  // Records the exception given from the debug agent.
  void HandleNotifyException(debug_ipc::NotifyException exception) override {
    exceptions_.push_back(std::move(exception));
    loop_->QuitNow();
  }

  void HandleNotifyThreadExiting(debug_ipc::NotifyThreadExiting thread) override {
    ASSERT_FALSE(thread_exited_);
    thread_exited_ = true;

    loop_->QuitNow();
  }

  void HandleNotifyProcessExiting(debug_ipc::NotifyProcessExiting exit) override {
    ASSERT_FALSE(process_exited_);
    process_exited_ = true;

    loop_->QuitNow();
  }

 private:
  debug::MessageLoop* loop_;
  uint64_t so_test_base_addr_ = 0;

  bool thread_started_ = false;
  bool thread_exited_ = false;

  bool process_exited_ = false;

  zx_koid_t process_koid_ = 0;
  zx_koid_t thread_koid_ = 0;

  std::vector<debug_ipc::NotifyException> exceptions_;
};

}  // namespace

// TODO(fxbug.dev/73422): This test fails, fix and re-enable.
TEST(BreakpointIntegration, DISABLED_SWBreakpoint) {
  // Uncomment for debugging the test.
  // debug::SetDebugMode(true);

  // We attempt to load the pre-made .so.
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  // Obtain the offsets into the .so of the symbols we want.
  uint64_t symbol_offset1 = so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName);
  ASSERT_NE(symbol_offset1, 0u);
  uint64_t symbol_offset2 = so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName2);
  ASSERT_NE(symbol_offset2, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();
    // This stream backend will take care of intercepting the calls from the debug agent.
    BreakpointStreamBackend mock_stream_backend(loop);

    DebugAgent agent(std::make_unique<ZirconSystemInterface>());
    RemoteAPI* remote_api = &agent;

    agent.Connect(&mock_stream_backend.stream());

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.argv.push_back(kTestExecutablePath);
    launch_request.inferior_type = debug_ipc::InferiorType::kBinary;
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_TRUE(launch_reply.status.ok());

    // We run the loop which will stop at the new thread notification.
    loop->Run();

    // We should have only received a thread started notification.
    ASSERT_TRUE(mock_stream_backend.thread_started());
    ASSERT_TRUE(mock_stream_backend.exceptions().empty());
    ASSERT_FALSE(mock_stream_backend.thread_exited());

    // We resume the thread because the new thread will be stopped.
    debug_ipc::ResumeRequest resume_request;
    resume_request.ids.push_back({.process = mock_stream_backend.process_koid(), .thread = 0});
    debug_ipc::ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);

    // We run the loop to get the notifications sent by the agent.
    // The stream backend will stop the loop once it has received the modules notification.
    loop->Run();

    // We should have found the correct module by now.
    ASSERT_NE(mock_stream_backend.so_test_base_addr(), 0u);

    DEBUG_LOG(Test) << "Modules found. Adding breakpoint.";

    // We get the offset of the loaded function within the process space.
    uint64_t module_base = mock_stream_backend.so_test_base_addr();
    uint64_t module_function1 = module_base + symbol_offset1;
    uint64_t module_function2 = module_base + symbol_offset2;

    // We add a breakpoint in the functions.
    constexpr uint32_t kBreakpointId = 1234u;
    debug_ipc::AddOrChangeBreakpointRequest breakpoint_request = {};
    breakpoint_request.breakpoint.id = kBreakpointId;
    breakpoint_request.breakpoint.one_shot = false;

    debug_ipc::ProcessBreakpointSettings location1 = {};
    location1.id.process = launch_reply.process_id;
    location1.address = module_function1;
    breakpoint_request.breakpoint.locations.push_back(location1);
    debug_ipc::ProcessBreakpointSettings location2 = {};
    location2.id.process = launch_reply.process_id;
    location2.address = module_function2;
    breakpoint_request.breakpoint.locations.push_back(location2);

    debug_ipc::AddOrChangeBreakpointReply breakpoint_reply;
    remote_api->OnAddOrChangeBreakpoint(breakpoint_request, &breakpoint_reply);
    ASSERT_TRUE(breakpoint_reply.status.ok());

    // Resume the process now that the breakpoint is installed.
    DEBUG_LOG(Test) << "Resuming thread.";
    remote_api->OnResume(resume_request, &resume_reply);
    loop->Run();

    // We should have received a breakpoint exception by now.
    ASSERT_EQ(mock_stream_backend.exceptions().size(), 1u);
    debug_ipc::NotifyException exception = mock_stream_backend.exceptions()[0];
    EXPECT_EQ(exception.thread.id.process, launch_reply.process_id);
    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kSoftwareBreakpoint);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);
    EXPECT_TRUE(exception.other_affected_threads.empty());  // Test has only one thread.

    // Verify that the correct breakpoint was hit.
    auto& breakpoint = exception.hit_breakpoints[0];
    EXPECT_EQ(breakpoint.id, kBreakpointId);
    EXPECT_EQ(breakpoint.hit_count, 1u);
    EXPECT_FALSE(breakpoint.should_delete);

    // Resuming the thread.
    DEBUG_LOG(Test) << "First breakpoint found, resuming thread.";
    remote_api->OnResume(resume_request, &resume_reply);
    loop->Run();

    // We should've received a second breakpoint exception.
    ASSERT_EQ(mock_stream_backend.exceptions().size(), 2u);
    exception = mock_stream_backend.exceptions()[1];
    EXPECT_EQ(exception.thread.id.process, launch_reply.process_id);
    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kSoftwareBreakpoint);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);

    // Verify that the correct breakpoint was hit.
    breakpoint = exception.hit_breakpoints[0];
    EXPECT_EQ(breakpoint.id, kBreakpointId);
    EXPECT_EQ(breakpoint.hit_count, 2u);
    EXPECT_FALSE(breakpoint.should_delete);

    // Resuming the thread.
    DEBUG_LOG(Test) << "Second breakpoint found, resuming thread.";
    remote_api->OnResume(resume_request, &resume_reply);
    loop->Run();

    // We verify that the thread exited or the process exited.
    ASSERT_TRUE(mock_stream_backend.thread_exited() || mock_stream_backend.process_exited());
  }
}

#if defined(__aarch64__)
// TODO(donosoc): Currently arm64 has a flake over this functionality.
//                One of the objectives of test week is to fix this flake once
//                and for all.
TEST(BreakpointIntegration, DISABLED_HWBreakpoint) {
#else
TEST(BreakpointIntegration, DISABLED_HWBreakpoint) {
#endif

  // We attempt to load the pre-made .so.
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  uint64_t symbol_offset = so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName);
  ASSERT_NE(symbol_offset, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();

    // This stream backend will take care of intercepting the calls from the debug agent.
    BreakpointStreamBackend mock_stream_backend(loop);

    DebugAgent agent(std::make_unique<ZirconSystemInterface>());
    RemoteAPI* remote_api = &agent;

    agent.Connect(&mock_stream_backend.stream());

    DEBUG_LOG(Test) << "Launching binary.";

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.inferior_type = debug_ipc::InferiorType::kBinary;
    launch_request.argv.push_back(kTestExecutablePath);
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_TRUE(launch_reply.status.ok());

    // We run the loop which will stop at the new thread notification.
    loop->Run();

    // We should have only received a thread started notification.
    ASSERT_TRUE(mock_stream_backend.thread_started());
    ASSERT_TRUE(mock_stream_backend.exceptions().empty());
    ASSERT_FALSE(mock_stream_backend.thread_exited());

    // We resume the thread because the new thread will be stopped.
    debug_ipc::ResumeRequest resume_request;
    resume_request.ids.push_back({.process = mock_stream_backend.process_koid(), .thread = 0});
    debug_ipc::ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);

    // We run the loop to get the notifications sent by the agent.
    // The stream backend will stop the loop once it has received the modules notification.
    loop->Run();

    // We should have found the correct module by now.
    ASSERT_NE(mock_stream_backend.so_test_base_addr(), 0u);

    // We get the offset of the loaded function within the process space.
    uint64_t module_base = mock_stream_backend.so_test_base_addr();
    uint64_t module_function = module_base + symbol_offset;

    DEBUG_LOG(Test) << "Setting breakpoint at 0x" << std::hex << module_function;

    // We add a breakpoint in that address.
    constexpr uint32_t kBreakpointId = 1234u;
    debug_ipc::ProcessBreakpointSettings location = {};
    location.id.process = launch_reply.process_id;
    location.address = module_function;

    debug_ipc::AddOrChangeBreakpointRequest breakpoint_request = {};
    breakpoint_request.breakpoint.id = kBreakpointId;
    breakpoint_request.breakpoint.type = debug_ipc::BreakpointType::kHardware;
    breakpoint_request.breakpoint.one_shot = true;
    breakpoint_request.breakpoint.locations.push_back(location);
    debug_ipc::AddOrChangeBreakpointReply breakpoint_reply;
    remote_api->OnAddOrChangeBreakpoint(breakpoint_request, &breakpoint_reply);
    ASSERT_TRUE(breakpoint_reply.status.ok());

    // Resume the process now that the breakpoint is installed.
    remote_api->OnResume(resume_request, &resume_reply);

    // The loop will run until the stream backend receives an exception notification.
    loop->Run();

    DEBUG_LOG(Test) << "Hit breakpoint.";

    // We should have received an exception now.
    ASSERT_EQ(mock_stream_backend.exceptions().size(), 1u);
    debug_ipc::NotifyException exception = mock_stream_backend.exceptions()[0];
    EXPECT_EQ(exception.thread.id.process, launch_reply.process_id);
    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kHardwareBreakpoint)
        << "Got: " << debug_ipc::ExceptionTypeToString(exception.type);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);

    // Verify that the correct breakpoint was hit.
    auto& breakpoint = exception.hit_breakpoints[0];
    EXPECT_EQ(breakpoint.id, kBreakpointId);
    EXPECT_EQ(breakpoint.hit_count, 1u);
    EXPECT_TRUE(breakpoint.should_delete);

    // Resume the thread again.
    remote_api->OnResume(resume_request, &resume_reply);
    loop->Run();

    DEBUG_LOG(Test) << "Verifyint thread exited correctly.";

    // We verify that the thread exited or the process exited.
    ASSERT_TRUE(mock_stream_backend.thread_exited() || mock_stream_backend.process_exited());
  }
}

}  // namespace debug_agent
