// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_BINARY_LAUNCHER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_BINARY_LAUNCHER_H_

#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/debug/debug_agent/stdio_handles.h"
#include "src/developer/debug/shared/status.h"

namespace debug_agent {

class ProcessHandle;

// This class is designed to help two-phase process creation, where a process needs to be setup, but
// before starting it that process needs to be attached to and registered with the DebugAgent.
class BinaryLauncher {
 public:
  virtual ~BinaryLauncher() = default;

  // Setup will create the process object but not launch the process yet.
  virtual debug::Status Setup(const std::vector<std::string>& argv) = 0;

  // It is possibly that Setup fails to obtain valid sockets from the process being launched. If
  // that is the case, both sockets will be in the initial state (ie. is_valid() == false).
  virtual StdioHandles ReleaseStdioHandles() = 0;

  // Accessor for a copy of the process handle, valid between Setup() and Start().
  virtual std::unique_ptr<ProcessHandle> GetProcess() const = 0;

  // Completes process launching.
  virtual debug::Status Start() = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_BINARY_LAUNCHER_H_
