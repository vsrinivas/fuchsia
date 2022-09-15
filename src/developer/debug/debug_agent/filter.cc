// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/filter.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <string_view>

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/job_handle.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/ipc/filter_utils.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

bool Filter::MatchesProcess(const ProcessHandle& process, SystemInterface& system_interface) const {
  if (filter_.job_koid) {
    zx_koid_t job_koid = process.GetJobKoid();
    while (job_koid && job_koid != filter_.job_koid) {
      job_koid = system_interface.GetParentJobKoid(job_koid);
    }
    if (job_koid != filter_.job_koid) {
      return false;
    }
  }
  return debug_ipc::FilterMatches(
      filter_, process.GetName(),
      system_interface.GetComponentManager().FindComponentInfo(process));
}

bool Filter::MatchesComponent(const std::string& moniker, const std::string& url) const {
  if (filter_.type == debug_ipc::Filter::Type::kComponentMoniker ||
      filter_.type == debug_ipc::Filter::Type::kComponentName ||
      filter_.type == debug_ipc::Filter::Type::kComponentUrl) {
    return debug_ipc::FilterMatches(filter_, "",
                                    debug_ipc::ComponentInfo{.moniker = moniker, .url = url});
  }
  return false;
}

std::vector<zx_koid_t> Filter::ApplyToJob(const JobHandle& job,
                                          SystemInterface& system_interface) const {
  std::vector<zx_koid_t> res;
  std::function<void(const JobHandle& job)> visit_each_job = [&](const JobHandle& job) {
    for (const auto& process : job.GetChildProcesses()) {
      if (MatchesProcess(*process, system_interface)) {
        res.push_back(process->GetKoid());
      }
    }
    for (const auto& child : job.GetChildJobs()) {
      visit_each_job(*child);
    }
  };
  visit_each_job(job);
  return res;
}

}  // namespace debug_agent
