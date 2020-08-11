// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_DEBUG_AGENT_HARNESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_DEBUG_AGENT_HARNESS_H_

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/mock_job_tree.h"
#include "src/developer/debug/debug_agent/mock_stream_backend.h"
#include "src/developer/debug/debug_agent/mock_system_interface.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

class MockProcess;

// Sets up a debug agent with a default mock interface for testing.
//
// This class also provides some helper functions to aid tests sending fake IPC requests without
// having to pack and unpack all of the structs.
//
// Typical setup:
//
//   MockDebugAgentHarness harness;
//
//   constexpr zx_koid_t kProcKoid = 1234;
//   MockProcess* process = harness.AddProcess(kProcKoid);
//   constexpr zx_koid_t kThreadKoid = 1235;
//   MockThread* thread = process->AddThread(kThreadKoid);
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

  // Adds a mocked process to the debug agent.
  MockProcess* AddProcess(zx_koid_t process_koid);

  // Convenience wrappers around IPC requests.
  zx_status_t AddOrChangeBreakpoint(
      uint32_t breakpoint_id, zx_koid_t process_koid, uint64_t address,
      debug_ipc::BreakpointType type = debug_ipc::BreakpointType::kSoftware);
  zx_status_t AddOrChangeBreakpoint(
      uint32_t breakpoint_id, zx_koid_t process_koid, zx_koid_t thread_koid,
      const debug_ipc::AddressRange& range,
      debug_ipc::BreakpointType type = debug_ipc::BreakpointType::kSoftware);
  void Pause(zx_koid_t process_koid = 0, zx_koid_t thread_koid = 0);
  void Resume(
      debug_ipc::ResumeRequest::How how = debug_ipc::ResumeRequest::How::kResolveAndContinue,
      zx_koid_t process_koid = 0, zx_koid_t thread_koid = 0);

 private:
  MockStreamBackend stream_backend_;
  MockSystemInterface* system_interface_;  // Owned by |agent_|.
  DebugAgent agent_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_DEBUG_AGENT_HARNESS_H_
