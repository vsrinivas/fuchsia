// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_job.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/component_utils.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/regex.h"

namespace debug_agent {

DebuggedJob::DebuggedJob(ProcessStartHandler* handler, std::unique_ptr<JobHandle> job_handle)
    : handler_(handler), job_handle_(std::move(job_handle)) {}

DebuggedJob::~DebuggedJob() = default;

debug::Status DebuggedJob::Init() {
  // Register for debug exceptions. Since this class owks the job_handle_ it is safe to capture
  // |this| here.
  return job_handle_->WatchJobExceptions(
      [this](std::unique_ptr<ProcessHandle> process) { OnProcessStarting(std::move(process)); });
}

bool DebuggedJob::FilterInfo::Matches(const std::string& proc_name) {
  if (regex.valid()) {
    return regex.Match(proc_name);
  }

  // TODO(fxbug.dev/5796): Job filters should always be valid.
  return proc_name.find(filter) != std::string::npos;
}

void DebuggedJob::OnProcessStarting(std::unique_ptr<ProcessHandle> process) {
  auto proc_name = process->GetName();

  // Tools like fx serve will connect every second or so to the target, spamming logging for this
  // with a lot of "/boot/bin/sh" starting. We filter this out as it makes debugging much harder.
  if (proc_name != "/boot/bin/sh") {
    DEBUG_LOG(Job) << "Debugged job " << koid() << ": Process " << proc_name << " starting.";
  }

  // Search through the available filters. If the regex is not valid, fallback to checking if
  // |proc_name| contains the filter.
  FilterInfo* matching_filter = nullptr;
  for (auto& filter : filters_) {
    if (filter.Matches(proc_name)) {
      matching_filter = &filter;
      break;
    }
  }

  if (matching_filter) {
    DEBUG_LOG(Job) << "Filter " << matching_filter->filter << " matches process " << proc_name
                   << ". Attaching.";
    handler_->OnProcessStart(matching_filter->filter, std::move(process));
  }
}

void DebuggedJob::ApplyToJob(FilterInfo& filter, JobHandle& job, ProcessHandleSetByKoid& matches) {
  for (std::unique_ptr<ProcessHandle>& proc : job.GetChildProcesses()) {
    if (filter.Matches(proc->GetName())) {
      DEBUG_LOG(Job) << "Filter " << filter.filter << " matches process " << proc->GetName();
      matches.insert(std::move(proc));
    }
  }

  for (std::unique_ptr<JobHandle>& child_job : job.GetChildJobs())
    ApplyToJob(filter, *child_job, matches);
}

DebuggedJob::ProcessHandleSetByKoid DebuggedJob::SetFilters(std::vector<std::string> filters) {
  filters_.clear();
  filters_.reserve(filters.size());

  ProcessHandleSetByKoid matches;

  for (auto& filter : filters) {
    // We check if this is a package url. If that is the case, me only need the component as a
    // filter, as the whole URL won't match.
    debug::ComponentDescription desc;
    if (debug::ExtractComponentFromPackageUrl(filter, &desc))
      filter = desc.component_name;

    debug::Regex regex;
    if (!regex.Init(filter))
      FX_LOGS(WARNING) << "Could not initialize regex for filter " << filter;

    DEBUG_LOG(Job) << "Debug job " << koid() << ": Adding filter " << filter;

    FilterInfo filter_info = {};
    filter_info.filter = std::move(filter);
    filter_info.regex = std::move(regex);
    ApplyToJob(filters_.emplace_back(std::move(filter_info)), *job_handle_, matches);
  }

  return matches;
}

void DebuggedJob::AppendFilter(std::string filter) {
  // We check whether this filter already exists.
  for (auto& existent_filter : filters_) {
    if (existent_filter.filter == filter)
      return;
  }

  debug::Regex regex;
  if (!regex.Init(filter)) {
    FX_LOGS(WARNING) << "Could not initialize regex for filter " << filter;
  }

  DEBUG_LOG(Job) << "Debugged job " << koid() << ": Appending filter " << filter;

  FilterInfo filter_info = {};
  filter_info.filter = std::move(filter);
  filter_info.regex = std::move(regex);
  filters_.push_back(std::move(filter_info));
}

}  // namespace debug_agent
