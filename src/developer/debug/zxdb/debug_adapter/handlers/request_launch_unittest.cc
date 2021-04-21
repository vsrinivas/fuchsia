// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_launch.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

namespace {

class RequestLaunchTest : public DebugAdapterContextTest {};

}  // namespace

TEST_F(RequestLaunchTest, LaunchInTerminal) {
  dap::LaunchRequestZxdb launch_req = {};
  // Dummy process to attach to.
  launch_req.process = "test";
  // Shell command to run the program. It is a list of args with first one being the command.
  launch_req.launchCommand = "fx run test";

  // Register client handler for RunInTerminal which will be called by server during Launch request.
  bool run_in_terminal_received = false;
  client().registerHandler([&](const dap::RunInTerminalRequest& req) {
    // Concatenate args and check of the command is same as launch command.
    std::string command;
    for_each(req.args.begin(), req.args.end(), [&command](const std::string& s) {
      if (!command.empty()) {
        command += ' ';
      }
      command += s;
    });
    EXPECT_EQ(launch_req.launchCommand, command);
    run_in_terminal_received = true;
    return dap::RunInTerminalResponse();
  });

  // Initialize session with RunInTerminal Supported.
  dap::InitializeRequest init_request = {};
  init_request.supportsRunInTerminalRequest = true;
  client().send(init_request);
  context().OnStreamReadable();

  // Run client twice to receive initialize response and event.
  RunClient();
  RunClient();

  // Send launch request from the client.
  auto response = client().send(launch_req);

  // Read request and process it in server.
  context().OnStreamReadable();

  // Run client to receive RunInTerminal request.
  RunClient();
  // Run client to receive launch response.
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
  EXPECT_TRUE(run_in_terminal_received);
}

TEST_F(RequestLaunchTest, LaunchNoTerminal) {
  // Register client handler for RunInTerminal which will be called by server during Launch request.
  bool run_in_terminal_received = false;
  client().registerHandler([&](const dap::RunInTerminalRequest& req) {
    run_in_terminal_received = true;
    return dap::RunInTerminalResponse();
  });

  InitializeDebugging();

  // Send attach request from the client.
  dap::LaunchRequestZxdb req = {};
  req.process = "test";
  auto response = client().send(req);

  // Read request and process it.
  context().OnStreamReadable();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  // Expect an error.
  EXPECT_TRUE(got.error);
  // Expect no RunInTerminal request.
  EXPECT_FALSE(run_in_terminal_received);
}

}  // namespace zxdb
