// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_BREAKPOINT_COMMAND_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_BREAKPOINT_COMMAND_TEST_H_

#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

// Shared test harness for logging breakpoint creations for testing the breakpoint-related
// commands.
//
// Tests should derive from BreakpointCommandTest and inspect
//
//   breakpoint_remote_api()->last_request
//
// for the last breakpoint add/modify message, and issue:
//
//   breakpoint_remote_api()->last_cb
//
// to mock the response from the debug agent.

class BreakpointTestRemoteAPI : public MockRemoteAPI {
 public:
  // RemoteAPI implementation.
  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
    last_request = request;
    last_cb = std::move(cb);
  }

  std::optional<debug_ipc::AddOrChangeBreakpointRequest> last_request;
  fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> last_cb;
};

class BreakpointCommandTest : public ConsoleTest {
 public:
  BreakpointTestRemoteAPI* breakpoint_remote_api() { return breakpoint_remote_api_; }

 private:
  // RemoteAPITest overrides.
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<BreakpointTestRemoteAPI>();
    breakpoint_remote_api_ = remote_api.get();
    return remote_api;
  }

  BreakpointTestRemoteAPI* breakpoint_remote_api_ = nullptr;  // Owned by the session.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_BREAKPOINT_COMMAND_TEST_H_
