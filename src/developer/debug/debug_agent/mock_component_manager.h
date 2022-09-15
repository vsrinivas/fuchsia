// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_

#include <map>

#include "src/developer/debug/debug_agent/component_manager.h"

namespace debug_agent {

class MockComponentManager : public ComponentManager {
 public:
  explicit MockComponentManager(SystemInterface* system_interface)
      : ComponentManager(system_interface) {}
  ~MockComponentManager() override = default;

  auto& component_info() { return component_info_; }

  // ComponentManager implementation.
  void SetDebugAgent(DebugAgent*) override {}

  std::optional<debug_ipc::ComponentInfo> FindComponentInfo(zx_koid_t job_koid) const override {
    if (auto it = component_info_.find(job_koid); it != component_info_.end())
      return it->second;
    return std::nullopt;
  }

  debug::Status LaunchComponent(const std::vector<std::string>& argv) override {
    return debug::Status("Not supported");
  }

  debug::Status LaunchTest(std::string url, std::vector<std::string> case_filters) override {
    return debug::Status("Not supported");
  }

  bool OnProcessStart(const ProcessHandle& process, StdioHandles* out_stdio,
                      std::string* process_name_override) override {
    return false;
  }

 private:
  std::map<zx_koid_t, debug_ipc::ComponentInfo> component_info_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_
