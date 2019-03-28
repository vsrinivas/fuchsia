// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_LAUNCHER_H_
#define GARNET_BIN_DEBUG_AGENT_LAUNCHER_H_

#include <string>
#include <vector>

#include <lib/zx/process.h>
#include <lib/zx/socket.h>

#include "garnet/lib/process/process_builder.h"
#include "lib/fxl/macros.h"
#include "lib/sys/cpp/service_directory.h"

namespace debug_agent {

// This class is designed to help two-phase process creation, where a process
// needs to be setup, but before starting it that process needs to be
// registered with the exception handler.
//
// Launchpad and our calling code have different semantics which makes a bit
// of a mismatch. Launchpad normally expects to work by doing setup and then
// returning ownership of its internal process handle at the end of launching.
// But our code needs to set up the exception handling before code starts
// executing, and expects to own the handle its using.
class BinaryLauncher {
 public:
  explicit BinaryLauncher(std::shared_ptr<sys::ServiceDirectory> env_services);
  ~BinaryLauncher();

  // Setup will create the process object but not launch the process yet.
  zx_status_t Setup(const std::vector<std::string>& argv);

  // It is possibly that Setup fails to obtain valid sockets from the process
  // being launched. If that is the case, both sockets will be in the initial
  // state (ie. is_valid() == false).
  zx::socket ReleaseStdout();
  zx::socket ReleaseStderr();

  // Accessor for a copy of the process handle, valid between Setup() and
  // Start().
  zx::process GetProcess() const;

  // Completes process launching.
  zx_status_t Start();

 private:
  // Creates a socket and passes it on to the builder as a FD handle.
  // |fd| should be a valid fd for the process to be created. Normally it will
  // be STDOUT_FILENO or STDERR_FILENO.
  //
  // Returns an empty socket if there was an error.
  zx::socket AddStdioEndpoint(int fd);

  process::ProcessBuilder builder_;

  // The stdout/stderr local socket endpoints.
  // Will be valid if Setup successfully transfered them to the process.
  zx::socket out_;
  zx::socket err_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BinaryLauncher);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_LAUNCHER_H_
