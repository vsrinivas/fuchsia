// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"
#include "src/developer/debug/debug_agent/integration_tests/mock_stream_backend.h"
#include "src/developer/debug/debug_agent/integration_tests/so_wrapper.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

namespace {

// This test is an integration test to verify that the debug agent is able to
// successfully set breakpoints to Zircon and get the correct responses.
// This particular test does the following script:
//
// 1. Load a pre-made .so (debug_agent_test_so) and search for a particular
//    exported function. By also getting the loaded base address of the .so, we
//    can get the offset of the function within the module.
//
// 2. Launch a process (through RemoteAPI::OnLaunch) control by the debug agent.
//
// 3. Get the module notication (NotifyModules message) for the process launched
//    in (2). We look over the modules for the same module (debug_agent_test_so)
//    that was loaded by this newly created process.
//    With the base address of this module, we can use the offset calculated in
//    (1) and get the actual loaded address for the exported function within
//    the process.
//
// 4. Set a breakpoint on that address and resume the process. The test program
//    is written such that it will call the searched symbol, so should hit the
//    breakpoint.
//
// 5. Verify that we get a breakpoint exception on that address.
//
// 6. Success!

// The exported symbol we're going to put the breakpoint on.
const char* kExportedFunctionName = "InsertBreakpointFunction";

// The test .so we load in order to search the offset of the exported symbol
// within it.
const char* kTestSo = "debug_agent_test_so.so";

// The test executable the debug agent is going to launch. This is linked with
// |kTestSo|, meaning that the offset within that .so will be valid into the
// loaded module of this executable.
/* const char* kTestExecutableName = "breakpoint_test_exe"; */
const char* kTestExecutablePath = "/pkg/bin/breakpoint_test_exe";
const char* kModuleToSearch = "libdebug_agent_test_so.so";

class BreakpointStreamBackend : public MockStreamBackend {
 public:
  BreakpointStreamBackend(debug_ipc::MessageLoop* loop) : loop_(loop) {}

  uint64_t so_test_base_addr() const { return so_test_base_addr_; }
  const debug_ipc::NotifyException& exception() const { return exception_; }
  const debug_ipc::NotifyThread& thread_notification() const {
    return thread_notification_;
  }
  void clear_thread_notification() { thread_notification_ = {}; }

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

  // Records the exception given from the debug agent.
  void HandleNotifyException(debug_ipc::NotifyException exception) override {
    exception_ = std::move(exception);
    loop_->QuitNow();
  }

  void HandleNotifyThreadStarting(debug_ipc::NotifyThread thread) override {
    thread_notification_ = std::move(thread);
    loop_->QuitNow();
  }
  void HandleNotifyThreadExiting(debug_ipc::NotifyThread thread) override {
    thread_notification_ = std::move(thread);
    loop_->QuitNow();
  }

 private:
  debug_ipc::MessageLoop* loop_;
  uint64_t so_test_base_addr_ = 0;
  debug_ipc::NotifyException exception_ = {};
  debug_ipc::NotifyThread thread_notification_ = {};
};

}  // namespace

TEST(BreakpointIntegration, SWBreakpoint) {
  // We attempt to load the pre-made .so.
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  uint64_t symbol_offset =
      so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName);
  ASSERT_NE(symbol_offset, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();
    // This stream backend will take care of intercepting the calls from the
    // debug agent.
    BreakpointStreamBackend mock_stream_backend(loop);
    RemoteAPI* remote_api = mock_stream_backend.remote_api();

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.argv.push_back(kTestExecutablePath);
    launch_request.inferior_type = debug_ipc::InferiorType::kBinary;
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_EQ(launch_reply.status, ZX_OK)
        << "Expected ZX_OK, Got: "
        << debug_ipc::ZxStatusToString(launch_reply.status);

    // We run the loop which will stop at the new thread notification.
    loop->Run();
    auto thread_notification = mock_stream_backend.thread_notification();
    ASSERT_EQ(thread_notification.record.process_koid, launch_reply.process_id);
    mock_stream_backend.clear_thread_notification();

    // We resume the thread because the new thread will be stopped.
    debug_ipc::ResumeRequest resume_request;
    resume_request.process_koid = thread_notification.record.process_koid;
    debug_ipc::ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);

    // We run the loop to get the notifications sent by the agent.
    // The stream backend will stop the loop once it has received the modules
    // notification.
    loop->Run();

    // We should have found the correct module by now.
    ASSERT_NE(mock_stream_backend.so_test_base_addr(), 0u);

    // We get the offset of the loaded function within the process space.
    uint64_t module_base = mock_stream_backend.so_test_base_addr();
    uint64_t module_function = module_base + symbol_offset;

    // We add a breakpoint in that address.
    constexpr uint32_t kBreakpointId = 1234u;
    debug_ipc::ProcessBreakpointSettings location = {};
    location.process_koid = launch_reply.process_id;
    location.address = module_function;

    debug_ipc::AddOrChangeBreakpointRequest breakpoint_request = {};
    breakpoint_request.breakpoint.breakpoint_id = kBreakpointId;
    breakpoint_request.breakpoint.one_shot = true;
    breakpoint_request.breakpoint.locations.push_back(location);
    debug_ipc::AddOrChangeBreakpointReply breakpoint_reply;
    remote_api->OnAddOrChangeBreakpoint(breakpoint_request, &breakpoint_reply);
    ASSERT_EQ(breakpoint_reply.status, ZX_OK);

    // Resume the process now that the breakpoint is installed.
    remote_api->OnResume(resume_request, &resume_reply);

    // The loop will run until the stream backend receives an exception
    // notification.
    loop->Run();

    // We should have received an exception now.
    debug_ipc::NotifyException exception = mock_stream_backend.exception();
    EXPECT_EQ(exception.thread.process_koid, launch_reply.process_id);
    EXPECT_EQ(exception.type, debug_ipc::NotifyException::Type::kSoftware);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);

    // Verify that the correct breakpoint was hit.
    auto& breakpoint = exception.hit_breakpoints[0];
    EXPECT_EQ(breakpoint.breakpoint_id, kBreakpointId);
    EXPECT_EQ(breakpoint.hit_count, 1u);
    EXPECT_TRUE(breakpoint.should_delete);
  }
}

TEST(BreakpointIntegration, HWBreakpoint) {
  // We attempt to load the pre-made .so.
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  uint64_t symbol_offset =
      so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName);
  ASSERT_NE(symbol_offset, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();

    // This stream backend will take care of intercepting the calls from the
    // debug agent.
    BreakpointStreamBackend mock_stream_backend(loop);
    RemoteAPI* remote_api = mock_stream_backend.remote_api();

    DEBUG_LOG(Test) << "Launching binary.";

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.inferior_type = debug_ipc::InferiorType::kBinary;
    launch_request.argv.push_back(kTestExecutablePath);
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_EQ(launch_reply.status, ZX_OK)
        << "Expected ZX_OK, Got: "
        << debug_ipc::ZxStatusToString(launch_reply.status);

    // We run the loop which will stop at the new thread notification.
    loop->Run();
    auto thread_notification = mock_stream_backend.thread_notification();
    ASSERT_EQ(thread_notification.record.process_koid, launch_reply.process_id);
    mock_stream_backend.clear_thread_notification();

    // We resume the thread because the new thread will be stopped.
    debug_ipc::ResumeRequest resume_request;
    resume_request.process_koid = thread_notification.record.process_koid;
    debug_ipc::ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);

    // We run the loop to get the notifications sent by the agent.
    // The stream backend will stop the loop once it has received the modules
    // notification.
    loop->Run();

    // We should have found the correct module by now.
    ASSERT_NE(mock_stream_backend.so_test_base_addr(), 0u);

    // We get the offset of the loaded function within the process space.
    uint64_t module_base = mock_stream_backend.so_test_base_addr();
    uint64_t module_function = module_base + symbol_offset;

    DEBUG_LOG(Test) << "Setting breakpoint at 0x" << std::hex
                    << module_function;

    // We add a breakpoint in that address.
    constexpr uint32_t kBreakpointId = 1234u;
    debug_ipc::ProcessBreakpointSettings location = {};
    location.process_koid = launch_reply.process_id;
    location.address = module_function;

    debug_ipc::AddOrChangeBreakpointRequest breakpoint_request = {};
    breakpoint_request.breakpoint.breakpoint_id = kBreakpointId;
    breakpoint_request.breakpoint.one_shot = true;
    breakpoint_request.breakpoint.type = debug_ipc::BreakpointType::kHardware;
    breakpoint_request.breakpoint.locations.push_back(location);
    debug_ipc::AddOrChangeBreakpointReply breakpoint_reply;
    remote_api->OnAddOrChangeBreakpoint(breakpoint_request, &breakpoint_reply);
    ASSERT_EQ(breakpoint_reply.status, ZX_OK)
        << "Received: " << debug_ipc::ZxStatusToString(breakpoint_reply.status);

    // Resume the process now that the breakpoint is installed.
    remote_api->OnResume(resume_request, &resume_reply);

    // The loop will run until the stream backend receives an exception
    // notification.
    loop->Run();

    DEBUG_LOG(Test) << "Hit breakpoint.";

    // We should have received an exception now.
    debug_ipc::NotifyException exception = mock_stream_backend.exception();
    EXPECT_EQ(exception.thread.process_koid, launch_reply.process_id);
    EXPECT_EQ(exception.type, debug_ipc::NotifyException::Type::kHardware)
        << "Got: " << debug_ipc::NotifyException::TypeToString(exception.type);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);

    // Verify that the correct breakpoint was hit.
    auto& breakpoint = exception.hit_breakpoints[0];
    EXPECT_EQ(breakpoint.breakpoint_id, kBreakpointId);
    EXPECT_EQ(breakpoint.hit_count, 1u);
    EXPECT_TRUE(breakpoint.should_delete);

    // Resume the thread again.
    remote_api->OnResume(resume_request, &resume_reply);
    loop->Run();

    DEBUG_LOG(Test) << "Verifyint thread exited correctly.";

    // We verify that the thread exited.
    thread_notification = mock_stream_backend.thread_notification();
    ASSERT_EQ(thread_notification.record.process_koid, launch_reply.process_id);
    auto& record = thread_notification.record;
    ASSERT_EQ(record.state, debug_ipc::ThreadRecord::State::kDead)
        << "Got: " << debug_ipc::ThreadRecord::StateToString(record.state);
  }
}

}  // namespace debug_agent
