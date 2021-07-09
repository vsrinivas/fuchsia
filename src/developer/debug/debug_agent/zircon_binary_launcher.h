// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_BINARY_LAUNCHER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_BINARY_LAUNCHER_H_

#include <lib/zx/process.h>
#include <lib/zx/socket.h>

#include "garnet/lib/process/process_builder.h"
#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/debug_agent/binary_launcher.h"

namespace debug_agent {

class ProcessHandle;

// Launchpad and our calling code have different semantics which makes a bit of a mismatch.
// Launchpad normally expects to work by doing setup and then returning ownership of its internal
// process handle at the end of launching. But our code needs to set up the exception handling
// before code starts executing, and expects to own the handle its using.
class ZirconBinaryLauncher final : public BinaryLauncher {
 public:
  explicit ZirconBinaryLauncher(std::shared_ptr<sys::ServiceDirectory> env_services);
  ~ZirconBinaryLauncher() override;

  debug::Status Setup(const std::vector<std::string>& argv) override;
  StdioHandles ReleaseStdioHandles() override;
  std::unique_ptr<ProcessHandle> GetProcess() const override;
  debug::Status Start() override;

 private:
  // Creates a socket and passes it on to the builder as a FD handle. |fd| should be a valid fd for
  // the process to be created. Normally it will be STDOUT_FILENO or STDERR_FILENO.
  //
  // Returns an empty socket if there was an error.
  zx::socket AddStdioEndpoint(int fd);

  process::ProcessBuilder builder_;

  // The stdout/stderr local socket endpoints. Will be valid if Setup successfully transferred them
  // to the process.
  StdioHandles stdio_handles_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_BINARY_LAUNCHER_H_
