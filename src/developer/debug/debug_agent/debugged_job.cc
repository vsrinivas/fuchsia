// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_job.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/shared/component_utils.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

DebuggedJob::DebuggedJob(ProcessStartHandler* handler, std::unique_ptr<JobHandle> job_handle)
    : handler_(handler), job_handle_(std::move(job_handle)) {}

DebuggedJob::~DebuggedJob() = default;

zx_status_t DebuggedJob::Init() {
  debug_ipc::MessageLoopTarget* loop = debug_ipc::MessageLoopTarget::Current();
  FX_DCHECK(loop);  // Loop must be created on this thread first.

  // Register for debug exceptions.
  debug_ipc::MessageLoopTarget::WatchJobConfig config;
  config.job_name = job_handle_->GetName();
  config.job_handle = job_handle_->GetNativeHandle().get();
  config.job_koid = koid();
  config.watcher = this;
  return loop->WatchJobExceptions(std::move(config), &job_watch_handle_);
}

bool DebuggedJob::FilterInfo::Matches(const std::string& proc_name) {
  if (regex.valid()) {
    return regex.Match(proc_name);
  }

  // TODO(fxbug.dev/5796): Job filters should always be valid.
  return proc_name.find(filter) != std::string::npos;
}

void DebuggedJob::OnProcessStarting(zx::exception exception_token,
                                    zx_exception_info_t exception_info) {
  // TODO(brettw) convert this to ExceptionHandle.
  zx_handle_t zircon_handle = ZX_HANDLE_INVALID;
  zx_status_t status = zx_exception_get_process(exception_token.get(), &zircon_handle);
  FX_DCHECK(status == ZX_OK) << "Got: " << debug_ipc::ZxStatusToString(status);

  std::unique_ptr<ProcessHandle> process =
      std::make_unique<ZirconProcessHandle>(zx::process(zircon_handle));
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

  // Attached to the process. At that point it will get a new thread notification for the initial
  // thread which it can stop or continue as it desires. Therefore, we can always resume the thread
  // in the "new process" exception.
  //
  // Technically it's not necessary to reset the handle, but being explicit here helps readability.
  exception_token.reset();
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
    debug_ipc::ComponentDescription desc;
    if (debug_ipc::ExtractComponentFromPackageUrl(filter, &desc))
      filter = desc.component_name;

    debug_ipc::Regex regex;
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

  debug_ipc::Regex regex;
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
