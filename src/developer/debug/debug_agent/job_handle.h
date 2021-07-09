// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_JOB_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_JOB_HANDLE_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/debug/shared/status.h"

namespace debug_agent {

class ProcessHandle;

class JobHandle {
 public:
  virtual ~JobHandle() = default;

  // Creates a copy of this job handle.
  virtual std::unique_ptr<JobHandle> Duplicate() const = 0;

  virtual zx_koid_t GetKoid() const = 0;
  virtual std::string GetName() const = 0;

  // Returns the set of child objects for this job.
  virtual std::vector<std::unique_ptr<JobHandle>> GetChildJobs() const = 0;
  virtual std::vector<std::unique_ptr<ProcessHandle>> GetChildProcesses() const = 0;

  // Registers for job exceptions. On success, the given callback will be issued for all process
  // launches in this job. Can be called with an empty function to unregister.
  virtual debug::Status WatchJobExceptions(
      fit::function<void(std::unique_ptr<ProcessHandle>)> cb) = 0;

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
