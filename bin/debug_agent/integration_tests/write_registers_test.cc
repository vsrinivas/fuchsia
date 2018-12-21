// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <gtest/gtest.h>

#include "garnet/bin/debug_agent/integration_tests/mock_stream_backend.h"
#include "garnet/bin/debug_agent/integration_tests/message_loop_wrapper.h"
#include "garnet/bin/debug_agent/integration_tests/so_wrapper.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/helper/zx_status.h"
#include "garnet/lib/debug_ipc/register_test_support.h"

// This tests verify that writing registers work.
// It does it by running a hand-made binary (test_data/*_register_test) that
// presents different scenarios that require changing registers in order to work
// properly.
//
// Current scenarios:
//
// x64 -------------------------------------------------------------------------
//
// 1. Branch on RAX:
//    This scenario hardcodes a SW breakpoint right before comparing RAX. If
//    unchanged, the program will call a function that will assert failure.
//    If RAX could be changed, the program will exit gracefully.
//
// 2. TODO(donosoc): Write a test that requires setting RIP to continue.
//
// arm64 -----------------------------------------------------------------------
//
// 1. TODO(donosoc): Write a test that jumps over comparing a register.
// 2. TODO(donosoc): Write a test that requires setting the to continue.

using namespace debug_ipc;

namespace debug_agent {

namespace {

const char* kTestExecutablePath = "/pkg/bin/write_register_test_exe";

// Receives messages from the debug agent and exposes relevant data.
class RegistersStreamBackend : public MockStreamBackend {
 public:
  RegistersStreamBackend(debug_ipc::MessageLoop* loop) : loop_(loop) {}

  const std::vector<NotifyException>& exceptions() const { return exceptions_; }
  const std::vector<NotifyThread>& thread_notifications() const {
    return thread_notifications_;
  }
  const std::optional<debug_ipc::NotifyProcess>& process_exit() const {
    return process_exit_;
  }

  // Records the exception given from the debug agent.
  void HandleNotifyException(debug_ipc::MessageReader* reader) override {
    debug_ipc::NotifyException exception;
    if (!debug_ipc::ReadNotifyException(reader, &exception))
      return;
    exceptions_.push_back(std::move(exception));
    loop_->QuitNow();
  }

  // Searches the loaded modules for specific one.
  void HandleNotifyModules(debug_ipc::MessageReader* reader) override {
    debug_ipc::NotifyModules modules;
    if (!debug_ipc::ReadNotifyModules(reader, &modules))
      return;
    loop_->QuitNow();
  }

  void HandleNotifyProcessExiting(debug_ipc::MessageReader* reader) override {
    debug_ipc::NotifyProcess process_exiting;
    if (!debug_ipc::ReadNotifyProcess(reader, &process_exiting))
      return;
    process_exit_ = process_exiting;
    loop_->QuitNow();
  }

  void HandleNotifyThreadStarting(debug_ipc::MessageReader* reader) override {
    NotifyThread thread;
    if (!ReadNotifyThread(reader, &thread))
      return;
    thread_notifications_.push_back(std::move(thread));
    loop_->QuitNow();
  }

 private:
  std::vector<NotifyThread> thread_notifications_;
  std::vector<NotifyException> exceptions_;
  std::optional<debug_ipc::NotifyProcess> process_exit_ = {};

  debug_ipc::MessageLoop* loop_;
};

}  // namespace

TEST(WriteRegisterTest, Write) {
#if defined(__aarch64__)
  // TODO(donosoc): Write arm64 test.
  return;
#endif

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();
    // This stream backend will take care of intercepting the calls from the
    // debug agent.
    RegistersStreamBackend stream_backend(loop);
    RemoteAPI* remote_api = stream_backend.remote_api();

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.argv.push_back(kTestExecutablePath);
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_EQ(launch_reply.status, static_cast<uint32_t>(ZX_OK));

    loop->Run();

    // We should get a thread notification.
    ASSERT_EQ(stream_backend.thread_notifications().size(), 1u);
    auto& thread_notification =
        stream_backend.thread_notifications().back();
    ASSERT_EQ(thread_notification.process_koid, launch_reply.process_koid);

    loop->Run();

    // We start the process.
    debug_ipc::ResumeRequest resume_request;
    resume_request.process_koid = launch_reply.process_koid;
    debug_ipc::ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);

    loop->Run();

    // We should have gotten a software exception.
    ASSERT_EQ(stream_backend.exceptions().size(), 1u);
    ASSERT_EQ(stream_backend.exceptions().back().type,
              NotifyException::Type::kSoftware);

    // Write the registers.
    WriteRegistersRequest write_reg_request;
    write_reg_request.process_koid = launch_reply.process_koid;
    write_reg_request.thread_koid = thread_notification.record.koid;
    write_reg_request.registers.push_back(
        CreateUint64Register(RegisterID::kX64_rax, 1u));

    WriteRegistersReply write_reg_reply;
    remote_api->OnWriteRegisters(write_reg_request, &write_reg_reply);

    ASSERT_EQ(write_reg_reply.status, static_cast<uint32_t>(ZX_OK))
        << ZxStatusToString(write_reg_reply.status);

    remote_api->OnResume(resume_request, &resume_reply);

    loop->Run();

    // We shouldn't have received a general exception.
    ASSERT_EQ(stream_backend.exceptions().size(), 1u);

    // We should have received a notification that the process exited with exit
    // code 0.
    ASSERT_TRUE(stream_backend.process_exit());
    EXPECT_EQ(stream_backend.process_exit()->process_koid,
              launch_reply.process_koid);
    EXPECT_EQ(stream_backend.process_exit()->return_code, 0u);
  }
}

}  // namespace debug_agent
