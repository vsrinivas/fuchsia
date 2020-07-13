// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/job_handle.h"

namespace debug_agent {

std::unique_ptr<ProcessHandle> JobHandle::FindProcess(zx_koid_t process_koid) const {
  // Search direct process descendents of this job.
  for (auto& proc : GetChildProcesses()) {
    if (proc->GetKoid() == process_koid)
      return std::move(proc);
  }

  // Recursively search child jobs.
  for (const auto& job : GetChildJobs()) {
    if (auto found = job->FindProcess(process_koid))
      return found;
  }

  return nullptr;
}

}  // namespace debug_agent
