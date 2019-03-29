// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_DEBUGGED_JOB_H_
#define GARNET_BIN_DEBUG_AGENT_DEBUGGED_JOB_H_

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/shared/zircon_exception_watcher.h"

#include "src/lib/fxl/macros.h"

namespace debug_agent {

class DebuggedJob;

class ProcessStartHandler {
 public:
  // This will pass ownership of both the process and the initial thread.
  // It's the job of the handler to eventually resume that thread.
  //
  // The DebuggedJob matching filter passed in case the handler is tracking from
  // which this event comes from.
  virtual void OnProcessStart(const std::string& filter,
                              zx::process process) = 0;
};

class DebuggedJob : public debug_ipc::ZirconExceptionWatcher {
 public:
  // Caller must call Init immediately after construction and delete the
  // object if that fails.
  DebuggedJob(ProcessStartHandler* handler, zx_koid_t job_koid, zx::job job);
  virtual ~DebuggedJob();

  zx_koid_t koid() const { return koid_; }
  zx::job& job() { return job_; }

  void SetFilters(std::vector<std::string> filters);
  void AppendFilter(std::string filter);

  // Returns ZX_OK on success. On failure, the object may not be used further.
  zx_status_t Init();

 private:
  // ZirconExceptionWatcher implementation.
  void OnProcessStarting(zx_koid_t job_koid, zx_koid_t process_koid,
                         zx_koid_t thread_koid) override;

  ProcessStartHandler* handler_;  // Non-owning.
  zx_koid_t koid_;
  zx::job job_;

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle job_watch_handle_;
  struct FilterInfo {
    std::string filter;
    // Filter used to compare against this filter.
    // We keep it around so we don't need to recompile it everytime we compare
    // against a new process.
    debug_ipc::Regex regex;
  };
  std::vector<FilterInfo> filters_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedJob);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_DEBUGGED_JOB_H_
