// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>
#include <memory>

#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "garnet/lib/debug_ipc/helper/test_stream_buffer.h"
#include "gtest/gtest.h"

namespace zxdb {

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
  // KOIDs. The thread will be repoted as running.
  Thread* InjectThread(uint64_t process_koid, uint64_t thread_koids);

  // Sends the exception notification to the session.
  void InjectException(const debug_ipc::NotifyException& exception);

 protected:
  // Derived classes implement this to provide their own IPC mocks. Ownership
  // will be transferred to the Session so it will be valid until TearDown
  // (most implementations will want to keep a pointer).
  virtual std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() = 0;

 private:
  debug_ipc::PlatformMessageLoop loop_;
  std::unique_ptr<Session> session_;
};

}  // namespace zxdb
