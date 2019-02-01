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

#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/zircon_exception_watcher.h"

#include "lib/fxl/macros.h"

namespace debug_agent {

class ProcessStartHandler {
 public:
  virtual void OnProcessStart(zx::process process) = 0;
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

  // Returns true on success. On failure, the object may not be used further.
  bool Init();

 private:
  // ZirconExceptionWatcher implementation.
  void OnProcessStarting(zx_koid_t job_koid, zx_koid_t process_koid,
                         zx_koid_t thread_koid) override;

  ProcessStartHandler* handler_;  // Non-owning.
  zx_koid_t koid_;
  zx::job job_;

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle job_watch_handle_;
  std::vector<std::string> filters_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedJob);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_DEBUGGED_JOB_H_
