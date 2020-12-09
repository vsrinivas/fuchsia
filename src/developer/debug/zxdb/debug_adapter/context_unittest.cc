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

}  // namespace zxdb
