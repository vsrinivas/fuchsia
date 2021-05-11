// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_next.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

namespace {

using RequestNextTest = DebugAdapterContextTest;

}  // namespace

TEST_F(RequestNextTest, NextStatement) {
  InitializeDebugging();

  InjectProcess(kProcessKoid);
  // Run client to receive process started event.
  RunClient();
  InjectThread(kProcessKoid, kThreadKoid);
  // Run client to receive threads started event.
  RunClient();

  constexpr uint64_t kAddress = 0x12345678;
  constexpr uint64_t kStack = 0x7890;

  // Notify of thread stop.
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  break_notification.thread.id = {.process = kProcessKoid, .thread = kThreadKoid};
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.emplace_back(kAddress, kStack, kStack);
  InjectException(break_notification);

  // Receive thread stopped event in client.
  RunClient();

  // Send next request from the client.
  dap::NextRequest request = {};
  request.threadId = kThreadKoid;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Run client to receive next response.
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
}

}  // namespace zxdb
