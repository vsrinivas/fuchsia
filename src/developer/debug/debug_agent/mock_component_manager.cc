// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_component_manager.h"

namespace debug_agent {

debug::Status MockComponentManager::LaunchComponent(DebuggedJob* root_job,
                                                    const std::vector<std::string>& argv,
                                                    uint64_t* component_id) {
  *component_id = 0;
  return debug::Status("Not supported");
}

uint64_t MockComponentManager::OnProcessStart(const std::string& filter, StdioHandles& out_stdio) {
  return 0;
}

}  // namespace debug_agent
