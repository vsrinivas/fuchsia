// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_COMPONENT_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_COMPONENT_MANAGER_H_

#include <optional>
#include <string>
#include <vector>

#include "src/developer/debug/debug_agent/stdio_handles.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/status.h"

namespace debug_agent {

class DebugAgent;
class Filter;
class ProcessHandle;
class SystemInterface;

// This class manages launching and monitoring Fuchsia components. It is a singleton owned by the
// DebugAgent.
class ComponentManager {
 public:
  // ComponentManager needs |SystemInterface::GetParentJobKoid| for |FindComponentInfo|.
  explicit ComponentManager(SystemInterface* system_interface)
      : system_interface_(system_interface) {}
  virtual ~ComponentManager() = default;

  // Find the component information if the job is the root job of an ELF component.
  virtual std::optional<debug_ipc::ComponentInfo> FindComponentInfo(zx_koid_t job_koid) const = 0;

  // Find the component information if the process runs in the context of a component.
  std::optional<debug_ipc::ComponentInfo> FindComponentInfo(const ProcessHandle& process) const;

  // Launches the component with the given command line.
  //
  // The component URL is in argv[0].
  virtual debug::Status LaunchComponent(const std::vector<std::string>& argv) = 0;

  // Launches a test. DebugAgent is needed here because Fuchsia test framework provides
  // stdout/stderr after the process starts, so we need to |GetDebuggedProcess| to |SetStdout|.
  virtual debug::Status LaunchTest(std::string url, std::vector<std::string> case_filters,
                                   DebugAgent* debug_agent) = 0;

  // Notification that a process has started.
  //
  // If the process starts because of a |LaunchComponent|, this function will fill in the given
  // stdio handles and return true.
  //
  // If it was not a component launch, returns false (the caller normally won't know if a launch is
  // a component without asking us, so it isn't necessarily an error).
  //
  // |process_name_override| allows the component manager to override the process name observed
  // by the client and is optional.
  virtual bool OnProcessStart(const ProcessHandle& process, StdioHandles* out_stdio,
                              std::string* process_name_override) = 0;

 private:
  SystemInterface* system_interface_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_COMPONENT_MANAGER_H_
