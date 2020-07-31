// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_DEBUG_AGENT_HARNESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_DEBUG_AGENT_HARNESS_H_

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/mock_job_tree.h"
#include "src/developer/debug/debug_agent/mock_stream_backend.h"
#include "src/developer/debug/debug_agent/mock_system_interface.h"

namespace debug_agent {

class MockDebugAgentHarness {
 public:
  MockDebugAgentHarness()
      : MockDebugAgentHarness(std::make_unique<MockSystemInterface>(*GetMockJobTree())) {}

  MockDebugAgentHarness(std::unique_ptr<MockSystemInterface> system_interface)
      : system_interface_(system_interface.get()), agent_(std::move(system_interface)) {
    agent_.Connect(&stream_backend_.stream());
  }

  DebugAgent* debug_agent() { return &agent_; }

  MockSystemInterface* system_interface() { return system_interface_; }

  MockStreamBackend* stream_backend() { return &stream_backend_; }

 private:
  MockStreamBackend stream_backend_;
  MockSystemInterface* system_interface_;  // Owned by |agent_|.
  DebugAgent agent_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_DEBUG_AGENT_HARNESS_H_
