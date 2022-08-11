// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/component_manager.h"

#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/debug_agent/system_interface.h"

namespace debug_agent {

std::optional<debug_ipc::ComponentInfo> ComponentManager::FindComponentInfo(
    const ProcessHandle& process) const {
  zx_koid_t job_koid = process.GetJobKoid();
  while (job_koid) {
    if (auto info = FindComponentInfo(job_koid); info)
      return info;
    job_koid = system_interface_->GetParentJobKoid(job_koid);
  }
  return std::nullopt;
}

}  // namespace debug_agent
