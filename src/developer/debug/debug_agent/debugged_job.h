// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_JOB_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_JOB_H_

#include <memory>
#include <set>
#include <vector>

#include "src/developer/debug/debug_agent/job_handle.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/shared/regex.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

class DebuggedJob;

class ProcessStartHandler {
 public:
  // During this call the thread will be stopped on the "start" exception and this exception will
  // be cleared when this call completes. If the implementation wants to keep the thread suspended,
  // it should manually suspend.
  virtual void OnProcessStart(std::unique_ptr<ProcessHandle> process) = 0;
};

class DebuggedJob {
 public:
  // Caller must call Init immediately after construction and delete the object if that fails.
  // The ProcessStartHandler pointer must outlive this class.
  DebuggedJob(ProcessStartHandler* handler, std::unique_ptr<JobHandle> job_handle);
  virtual ~DebuggedJob();

  const JobHandle& job_handle() const { return *job_handle_; }
  JobHandle& job_handle() { return *job_handle_; }

  zx_koid_t koid() const { return job_handle_->GetKoid(); }

  // Returns ZX_OK on success. On failure, the object may not be used further.
  debug::Status Init();

 private:
  ProcessStartHandler* handler_;  // Non-owning.
  std::unique_ptr<JobHandle> job_handle_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedJob);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_JOB_H_
