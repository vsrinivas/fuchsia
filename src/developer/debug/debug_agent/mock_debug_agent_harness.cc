// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_debug_agent_harness.h"

#include "src/developer/debug/debug_agent/mock_process.h"

namespace debug_agent {

MockProcess* MockDebugAgentHarness::AddProcess(zx_koid_t process_koid) {
  auto owning_process = std::make_unique<MockProcess>(debug_agent(), process_koid);
  MockProcess* result = owning_process.get();

  debug_agent()->InjectProcessForTest(std::move(owning_process));
  return result;
}

zx_status_t MockDebugAgentHarness::AddOrChangeBreakpoint(uint32_t breakpoint_id,
                                                         zx_koid_t process_koid, uint64_t address,
                                                         debug_ipc::BreakpointType type) {
  debug_ipc::ProcessBreakpointSettings location;
  location.process_koid = process_koid;
  location.address = address;

  debug_ipc::AddOrChangeBreakpointRequest request;
  request.breakpoint.id = breakpoint_id;
  request.breakpoint.type = type;
  request.breakpoint.name = "Injected breakpoint";
  request.breakpoint.locations.push_back(location);

  debug_ipc::AddOrChangeBreakpointReply reply;
  debug_agent()->OnAddOrChangeBreakpoint(request, &reply);
  return reply.status;
}

zx_status_t MockDebugAgentHarness::AddOrChangeBreakpoint(uint32_t breakpoint_id,
                                                         zx_koid_t process_koid,
                                                         zx_koid_t thread_koid,
                                                         const debug_ipc::AddressRange& range,
                                                         debug_ipc::BreakpointType type) {
  debug_ipc::ProcessBreakpointSettings location;
  location.process_koid = process_koid;
  location.thread_koid = thread_koid;
  location.address_range = range;

  debug_ipc::AddOrChangeBreakpointRequest request;
  request.breakpoint.id = breakpoint_id;
  request.breakpoint.type = type;
  request.breakpoint.name = "Injected breakpoint";
  request.breakpoint.locations.push_back(location);

  debug_ipc::AddOrChangeBreakpointReply reply;
  debug_agent()->OnAddOrChangeBreakpoint(request, &reply);
  return reply.status;
}

void MockDebugAgentHarness::Pause(zx_koid_t process_koid, zx_koid_t thread_koid) {
  debug_ipc::PauseRequest request;
  request.process_koid = process_koid;
  request.thread_koid = thread_koid;

  debug_ipc::PauseReply reply;
  debug_agent()->OnPause(request, &reply);
}

void MockDebugAgentHarness::Resume(debug_ipc::ResumeRequest::How how, zx_koid_t process_koid,
                                   zx_koid_t thread_koid) {
  debug_ipc::ResumeRequest request;
  request.how = how;
  request.process_koid = process_koid;
  if (thread_koid)
    request.thread_koids.push_back(thread_koid);

  debug_ipc::ResumeReply reply;
  debug_agent()->OnResume(request, &reply);
}

}  // namespace debug_agent
