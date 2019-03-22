// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/client/remote_api.h"
#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/test_stream_buffer.h"

namespace zxdb {

class Frame;
class Process;
class Session;
class Thread;

// This is a test harness for client tests that mock out the RemoteAPI. This
// class sets up a message loop and the necessary plumbing.
//
// The individual tests supply their own implementation of RemoteAPI.
class RemoteAPITest : public testing::Test {
 public:
  RemoteAPITest();
  virtual ~RemoteAPITest();

  // testing::Test implementation.
  void SetUp() override;
  void TearDown() override;

  debug_ipc::PlatformMessageLoop& loop() { return loop_; }
  Session& session() { return *session_; }

  // Makes the target have a fake running process with the given KOID. This
  // assumes there is only one target in the system and that it is not
  // currently running.
  Process* InjectProcess(uint64_t process_koid);

  // Sends a "thread created" notifications to the client for the given fake
  // KOID. The thread will be reported as running.
  Thread* InjectThread(uint64_t process_koid, uint64_t thread_koid);

  // Sends the exception notification to the session.
  void InjectException(const debug_ipc::NotifyException& exception);

  // Sends the exception notification and forces the given stack information.
  // This bypasses the normal thread metadata computation. The exception
  // address will be taken from the address of the top of the stack.
  //
  // If you use the one that takes a NotifyException, the calling code need not
  // populate the thread vector and stack amount, they will be ignored.
  void InjectExceptionWithStack(const debug_ipc::NotifyException& exception,
                                std::vector<std::unique_ptr<Frame>> frames,
                                bool has_all_frames);
  void InjectExceptionWithStack(
      uint64_t process_koid, uint64_t thread_koid,
      debug_ipc::NotifyException::Type exception_type,
      std::vector<std::unique_ptr<Frame>> frames, bool has_all_frames,
      const std::vector<debug_ipc::BreakpointStats>& breakpoints = {});

 protected:
  // Derived classes implement this to provide their own IPC mocks. Ownership
  // will be transferred to the Session so it will be valid until TearDown
  // (most implementations will want to keep a pointer).
  virtual std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() = 0;

  // Allows tests to override the architecture for the test to run in. Defaults
  // to x64.
  virtual debug_ipc::Arch GetArch() const { return debug_ipc::Arch::kX64; }

 private:
  debug_ipc::PlatformMessageLoop loop_;
  std::unique_ptr<Session> session_;
};

}  // namespace zxdb
