// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

TEST_F(DebugAdapterContextTest, InitializeRequest) {
  // Send initialize request from the client.
  auto response = client().send(dap::InitializeRequest{});

  // Read request and process it.
  context().OnStreamReadable();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_EQ(got.error, false);
  EXPECT_EQ(bool(got.response.supportsFunctionBreakpoints), true);
  EXPECT_EQ(bool(got.response.supportsConfigurationDoneRequest), true);
}

TEST_F(DebugAdapterContextTest, InitializedEvent) {
  bool event_received = false;
  client().registerHandler([&](const dap::InitializedEvent& arg) { event_received = true; });

  // Send initialize request from the client.
  auto response = client().send(dap::InitializeRequest{});
  context().OnStreamReadable();
  // Run client twice to receive response and event.
  RunClient();
  RunClient();
  EXPECT_TRUE(event_received);
}

TEST_F(DebugAdapterContextTest, ThreadStartExitEvent) {
  bool start_received = false;
  bool exit_received = false;

  client().registerHandler([&](const dap::ThreadEvent& arg) {
    EXPECT_EQ(arg.threadId, static_cast<dap::integer>(kThreadKoid));
    if (arg.reason == "started") {
      start_received = true;
    }
    if (arg.reason == "exited") {
      exit_received = true;
    }
  });

  InitializeDebugging();

  InjectProcess(kProcessKoid);
  InjectThread(kProcessKoid, kThreadKoid);

  // Receive thread started event in client.
  RunClient();
  EXPECT_TRUE(start_received);

  debug_ipc::NotifyThread notify;
  notify.record.process_koid = kProcessKoid;
  notify.record.thread_koid = kThreadKoid;
  notify.record.state = debug_ipc::ThreadRecord::State::kDying;
  session().DispatchNotifyThreadExiting(notify);

  // Receive thread exited event in client.
  RunClient();
  EXPECT_TRUE(exit_received);
}

TEST_F(DebugAdapterContextTest, StoppedEvent) {
  bool event_received = false;

  client().registerHandler([&](const dap::StoppedEvent& arg) {
    EXPECT_EQ(arg.reason, "breakpoint");
    EXPECT_TRUE(arg.threadId.has_value());
    EXPECT_EQ(arg.threadId.value(), static_cast<dap::integer>(kThreadKoid));
    event_received = true;
  });

  InitializeDebugging();

  InjectProcess(kProcessKoid);
  InjectThread(kProcessKoid, kThreadKoid);

  // Receive thread started event in client.
  RunClient();

  constexpr uint64_t kAddress = 0x12345678;
  constexpr uint64_t kStack = 0x7890;

  // Notify of thread stop.
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  break_notification.thread.process_koid = kProcessKoid;
  break_notification.thread.thread_koid = kThreadKoid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.emplace_back(kAddress, kStack, kStack);
  InjectException(break_notification);

  // Receive thread stopped event in client.
  RunClient();
  EXPECT_TRUE(event_received);
}

}  // namespace zxdb
