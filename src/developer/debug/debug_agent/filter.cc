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
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

namespace {

bool MatchComponentUrl(std::string_view url, std::string_view pattern) {
  // Only deals with the most common case: the target URL contains a hash but the pattern doesn't.
  // The hash will look like "?hash=xxx#".
  const char* hash = "?hash=";
  if (url.find(hash) != std::string_view::npos && url.find_last_of('#') != std::string_view::npos &&
      pattern.find(hash) == std::string_view::npos) {
    std::string new_url(url.substr(0, url.find(hash)));
    new_url += url.substr(url.find_last_of('#'));
    return new_url == pattern;
  }
  return url == pattern;
}

}  // namespace

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
  switch (filter_.type) {
    case debug_ipc::Filter::Type::kProcessNameSubstr:
      return process.GetName().find(filter_.pattern) != std::string::npos;
    case debug_ipc::Filter::Type::kProcessName:
      return process.GetName() == filter_.pattern;
    case debug_ipc::Filter::Type::kUnset:
      return false;
    default:
      break;  // Fall through
  }
  // All unhandled filter types at this point require component information.
  auto info = system_interface.GetComponentManager().FindComponentInfo(process);
  if (!info) {
    return false;
  }
  switch (filter_.type) {
    case debug_ipc::Filter::Type::kComponentName:
      return info->url.substr(info->url.find_last_of('/') + 1) == filter_.pattern;
    case debug_ipc::Filter::Type::kComponentUrl:
      return MatchComponentUrl(info->url, filter_.pattern);
    case debug_ipc::Filter::Type::kComponentMoniker:
      return info->moniker == filter_.pattern;
    default:
      break;
  }
  FX_NOTREACHED();
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
