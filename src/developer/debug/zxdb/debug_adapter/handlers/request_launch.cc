// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_launch.h"

#include <dap/types.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"

namespace dap {

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(LaunchRequestZxdb, LaunchRequest, "launch",
                                  DAP_FIELD(process, "process"),
                                  DAP_FIELD(launchCommand, "launchCommand"), DAP_FIELD(cwd, "cwd"))

}  // namespace dap

namespace zxdb {

void GetCommandArray(const dap::string& cmd_string, dap::array<dap::string>& cmd) {
  // Split command string at whitespaces to an array of strings.
  // This is required by RunInTerminal request.
  size_t split_pos = cmd_string.find(' ');
  size_t start_pos = 0;

  while (split_pos != std::string::npos) {
    cmd.push_back(cmd_string.substr(start_pos, split_pos - start_pos));
    start_pos = split_pos + 1;
    split_pos = cmd_string.find(' ', start_pos);
  }

  cmd.push_back(
      cmd_string.substr(start_pos, std::min(split_pos, cmd_string.size()) - start_pos + 1));
}

dap::ResponseOrError<dap::LaunchResponse> OnRequestLaunch(DebugAdapterContext* context,
                                                          const dap::LaunchRequestZxdb& req) {
  if (!context->supports_run_in_terminal()) {
    return dap::Error(
        "Client doesn't support run in terminal. Please launch program manually and use attach "
        "instead of launch to connect to zxdb.");
  }

  Filter* filter = context->session()->system().CreateNewFilter();
  filter->SetPattern(req.process);

  dap::RunInTerminalRequest run_request;
  run_request.title = "zxdb launch";
  run_request.kind = "integrated";
  GetCommandArray(req.launchCommand, run_request.args);
  if (req.cwd) {
    run_request.cwd = req.cwd.value();
  }
  // Send RunInTerminal request.
  // TODO(69387): Currently not waiting for the response from the client. Because the
  // response is returned as a future and waiting on it will block the MessageLoop creating a
  // deadlock, as MessageLoop should be running in order to receive the response. This can be fixed
  // by getting a response notification from cppdap.
  // Secondly, the response contains launched terminal process ID, but nothing about whether the
  // command ran successfully. It might be helpful to return error to Launch request after getting
  // error code(if any exists) from launched process.
  context->dap().send(run_request);

  return dap::LaunchResponse();
}

}  // namespace zxdb
