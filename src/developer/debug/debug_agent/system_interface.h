// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INTERFACE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INTERFACE_H_

#include "src/developer/debug/debug_agent/job_handle.h"
#include "src/developer/debug/debug_agent/process_handle.h"

namespace debug_agent {

// Abstract interface that represents the system. This is eqivalent to ProcessHandle for processes
// but for the system (for which there's not a clearly owned handle).
class SystemInterface {
 public:
  virtual ~SystemInterface() = default;

  // The root job may be invalid if there was an error connecting.
  const JobHandle& GetRootJob() const { return const_cast<SystemInterface*>(this)->GetRootJob(); }
  virtual JobHandle& GetRootJob() = 0;

  // Returns a handle to the process with the given koid. Returns an empty pointer if the process
  // was not found. This can also happen if the debug_agent doesn't have permission to see it.
  virtual std::unique_ptr<ProcessHandle> GetProcess(zx_koid_t process_koid) const = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INTERFACE_H_
