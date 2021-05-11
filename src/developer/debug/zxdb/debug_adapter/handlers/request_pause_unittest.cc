// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_pause.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

namespace {

class RequestPauseTest : public DebugAdapterContextTest {};

}  // namespace

TEST_F(RequestPauseTest, PauseThread) {
  bool paused = false;

  client().registerHandler([&paused](const dap::StoppedEvent& arg) {
    if (arg.threadId) {
      EXPECT_EQ(arg.threadId.value(), static_cast<dap::integer>(kThreadKoid));
    }
    if (arg.reason == "pause") {
      paused = true;
    }
  });

  InitializeDebugging();

  InjectProcess(kProcessKoid);
  // Run client to receive process started event.
  RunClient();
  InjectThread(kProcessKoid, kThreadKoid);
  // Run client to receive threads started event.
  RunClient();

  // Setup thread pause reply for RemoteApi
  debug_ipc::PauseReply reply;
  debug_ipc::ThreadRecord record;
  record.state = debug_ipc::ThreadRecord::State::kSuspended;
  record.id = {.process = kProcessKoid, .thread = kThreadKoid};
  reply.threads.push_back(record);
  mock_remote_api()->set_pause_reply(reply);

  // Send pause request from the client.
  dap::PauseRequest request = {};
  request.threadId = kThreadKoid;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  // Run client to receive pause response and thread stopped event.
  RunClient();
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
  EXPECT_TRUE(paused);
}

}  // namespace zxdb
