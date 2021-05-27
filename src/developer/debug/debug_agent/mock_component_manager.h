// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_

#include "src/developer/debug/debug_agent/component_manager.h"

namespace debug_agent {

class MockComponentManager : public ComponentManager {
 public:
  MockComponentManager() = default;
  ~MockComponentManager() override = default;

  // ComponentManager implementation.
  zx_status_t LaunchComponent(DebuggedJob* root_job, const std::vector<std::string>& argv,
                              uint64_t* component_id) override;
  uint64_t OnProcessStart(const std::string& filter, StdioHandles& out_stdio) override;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_COMPONENT_MANAGER_H_
