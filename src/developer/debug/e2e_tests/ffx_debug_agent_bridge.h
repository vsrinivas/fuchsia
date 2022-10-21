// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_E2E_TESTS_FFX_DEBUG_AGENT_BRIDGE_H_
#define SRC_DEVELOPER_DEBUG_E2E_TESTS_FFX_DEBUG_AGENT_BRIDGE_H_

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

// This class is to RAII the sub-process for FFX that will spawn the socket
// connections the host side of the debugger needs in order to speak to the FIDL
// protocol implemented in DebugAgent.
//
// Specifically, this will:
//   1. Create a UNIX pipe with two file descriptors.
//   2. Fork the process and exec `ffx debug connect --agent-only`.
//   3. The write end of the pipe will go to the child and dup STDOUT.
//   4. The read end of the pipe will go to the parent.
//   5. Close both the read and write ends of the pipe in the parent.
//   6. Close both the read and write ends of the pipe in the child.
//   7. After all tests have run, this will be destructed and issue a SIGTERM to
//      the ffx command to clean up the DebugAgent socket and files.
class FfxDebugAgentBridge {
 public:
  FfxDebugAgentBridge(char* prog_name, char** unix_env)
      : program_name_(prog_name), unix_env_(unix_env) {}
  ~FfxDebugAgentBridge();

  // It is expected that this method is called once per test executable, and that many test cases
  // can be run with this object constructed before all cases and destructed after all cases.
  // Calling this method will involve fork-ing and exec-ing `ffx debug connect --agent-only`
  // with additional necessary parameters in infra builds to determine the proper target from the
  // environment. Locally, it is assumed that `fx set-device` has been used to configure which
  // device to use.
  Err Init();

  std::string_view GetDebugAgentSocketPath() const { return socket_path_; }

 private:
  // Fork the child process with the pipe file descriptors configured to send
  // the STDOUT of child to the write end of the pipe and the parent to wait
  // for the read end of the pipe.
  //
  // Returns an Err containing the stringified errno value on failure.
  Err SetupPipeAndFork(char** unix_env);

  // Reads the path to the UNIX socket created by the ffx sub-process from
  // |pipe_read_end_|.
  //
  // Returns an Err containing the stringified errno value on failure.
  Err ReadDebugAgentSocketPath();

  // Close all remaining open file descriptors, and send SIGTERM to the child
  // process.
  //
  // Returns an Err containing the stringified errno value on failure.
  Err CleanupChild() const;

  int pipe_read_end_ = 0;
  int pipe_write_end_ = 0;
  int child_pid_ = 0;

  std::string program_name_;
  std::string socket_path_;

  char** unix_env_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FfxDebugAgentBridge);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_E2E_TESTS_FFX_DEBUG_AGENT_BRIDGE_H_
