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

debug::Status MockComponentManager::LaunchComponent(const std::vector<std::string>& argv) {
  return debug::Status("Not supported");
}

bool MockComponentManager::OnProcessStart(const ProcessHandle& process, StdioHandles* out_stdio) {
  return false;
}

}  // namespace debug_agent
