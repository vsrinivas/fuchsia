// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_VSH_COMMAND_RUNNER_H_
#define SRC_VIRTUALIZATION_LIB_VSH_COMMAND_RUNNER_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fit/result.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace vsh {

// Enable running commands using vsh.
//
// Each command to be run will have its own vsh connection, which enables all
// stdout/stderr and return code to be captured.
//
// Note that this blocking interface will buffer all stdout/stderr for the
// process until it exits and is not suitable for processes that produce a
// large amount of stdout/stderr output.
class BlockingCommandRunner {
 public:
  BlockingCommandRunner(
      fidl::InterfaceHandle<fuchsia::virtualization::HostVsockEndpoint>
          socket_endpoint,
      uint32_t cid, uint32_t port = 9001);

  struct Command {
    // The command to be run. The executable must be at argv[0].
    std::vector<std::string> argv;
    // Any environment variables to set for this command execution.
    std::unordered_map<std::string, std::string> env;
  };

  struct CommandResult {
    // The stdout for the process.
    std::string out;
    // The stderr for the process.
    std::string err;
    // The exit code for the process.
    int32_t return_code;
  };

  // Sends a command over vsh to be executed and returns the output.
  //
  // Note this is a blocking API that won't return until the process started
  // by |command| has exited, or the vsh connection has been closed.
  fit::result<CommandResult, zx_status_t> Execute(Command command);

 private:
  fuchsia::virtualization::HostVsockEndpointSyncPtr socket_endpoint_;
  uint32_t cid_;
  uint32_t port_;
};

}  // namespace vsh

#endif  // SRC_VIRTUALIZATION_LIB_VSH_COMMAND_RUNNER_H_
