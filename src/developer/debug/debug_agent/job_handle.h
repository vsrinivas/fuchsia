// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_JOB_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_JOB_HANDLE_H_

#include <lib/zx/job.h>

#include <memory>
#include <vector>

#include "src/developer/debug/debug_agent/process_handle.h"

namespace debug_agent {

class JobHandle {
 public:
  virtual ~JobHandle() = default;

  // Creates a copy of this job handle.
  virtual std::unique_ptr<JobHandle> Duplicate() const = 0;

  // Access to the underlying native job object. This is for porting purposes, ideally this object
  // would encapsulate all details about the job for testing purposes and this getter would be
  // removed. In testing situations, the returned value may be an empty object,
  // TODO(brettw) Remove this.
  virtual const zx::job& GetNativeHandle() const = 0;
  virtual zx::job& GetNativeHandle() = 0;

  virtual zx_koid_t GetKoid() const = 0;
  virtual std::string GetName() const = 0;

  // Returns the set of child objects for this job.
  virtual std::vector<std::unique_ptr<JobHandle>> GetChildJobs() const = 0;
  virtual std::vector<std::unique_ptr<ProcessHandle>> GetChildProcesses() const = 0;

  // Recursively searches the job tree from this job/process and returns a handle to it. Returns a
  // null pointer if the job/process was not found. This can also happen if the debug_agent doesn't
  // have permission to see it.
  //
  // This is not virtual because it can be implemented entirely in terms of the virtual interface.
  std::unique_ptr<JobHandle> FindJob(zx_koid_t job_koid) const;
  std::unique_ptr<ProcessHandle> FindProcess(zx_koid_t process_koid) const;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_JOB_HANDLE_H_
