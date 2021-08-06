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
  //
  // The matching filter is passed in case the handler is tracking from which this event comes from.
  virtual void OnProcessStart(const std::string& filter,
                              std::unique_ptr<ProcessHandle> process) = 0;
};

class DebuggedJob {
 public:
  struct FilterInfo {
    std::string filter;
    // Filter used to compare against this filter. We keep it around so we don't need to recompile
    // it every time we compare against a new process.
    debug::Regex regex;

    bool Matches(const std::string& proc_name);
  };

  // Declare a set of processes, unique-ified by koid.
  struct CompareProcessHandleByKoid {
    bool operator()(const std::unique_ptr<ProcessHandle>& left,
                    const std::unique_ptr<ProcessHandle>& right) const {
      return left->GetKoid() < right->GetKoid();
    }
  };
  using ProcessHandleSetByKoid =
      std::set<std::unique_ptr<ProcessHandle>, CompareProcessHandleByKoid>;

  // Caller must call Init immediately after construction and delete the object if that fails.
  // The ProcessStartHandler pointer must outlive this class.
  DebuggedJob(ProcessStartHandler* handler, std::unique_ptr<JobHandle> job_handle);
  virtual ~DebuggedJob();

  const JobHandle& job_handle() const { return *job_handle_; }
  JobHandle& job_handle() { return *job_handle_; }

  zx_koid_t koid() const { return job_handle_->GetKoid(); }

  // Returns the set of processes that matches any of the |filters|.
  ProcessHandleSetByKoid SetFilters(std::vector<std::string> filters);

  void AppendFilter(std::string filter);
  const std::vector<FilterInfo>& filters() const { return filters_; }

  // Returns ZX_OK on success. On failure, the object may not be used further.
  debug::Status Init();

 private:
  void OnProcessStarting(std::unique_ptr<ProcessHandle> process);

  // Computes the set of currently running processes that matches any of the |filters|.
  void ApplyToJob(FilterInfo& filter, JobHandle& job, ProcessHandleSetByKoid& matches);

  ProcessStartHandler* handler_;  // Non-owning.
  std::unique_ptr<JobHandle> job_handle_;

  // Handle for watching the process exceptions.
  std::vector<FilterInfo> filters_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedJob);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_JOB_H_
