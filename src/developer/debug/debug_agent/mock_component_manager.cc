// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_component_manager.h"

#include <optional>

namespace debug_agent {

std::optional<debug_ipc::ComponentInfo> MockComponentManager::FindComponentInfo(
    zx_koid_t job_koid) const {
  if (auto it = component_info_.find(job_koid); it != component_info_.end())
    return it->second;
  return std::nullopt;
}

debug::Status MockComponentManager::LaunchComponent(DebugAgent& debug_agent,
                                                    const std::vector<std::string>& argv,
                                                    uint64_t* component_id) {
  *component_id = 0;
  return debug::Status("Not supported");
}

uint64_t MockComponentManager::OnProcessStart(const Filter& filter, StdioHandles& out_stdio) {
  return 0;
}

}  // namespace debug_agent
