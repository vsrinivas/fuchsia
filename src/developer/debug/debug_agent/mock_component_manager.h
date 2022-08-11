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
  MockComponentManager() = default;
  ~MockComponentManager() override = default;

  auto& component_info() { return component_info_; }

  // ComponentManager implementation.
  std::optional<debug_ipc::ComponentInfo> FindComponentInfo(zx_koid_t job_koid) const override;
  debug::Status LaunchComponent(const std::vector<std::string>& argv) override;
  bool OnProcessStart(const ProcessHandle& process, StdioHandles* out_stdio) override;

 private:
  std::map<zx_koid_t, debug_ipc::ComponentInfo> component_info_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_
