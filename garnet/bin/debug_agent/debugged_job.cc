// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_target.h"

#include "garnet/bin/debug_agent/debugged_job.h"
#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/bin/debug_agent/system_info.h"
#include "garnet/lib/debug_ipc/debug/logging.h"
#include "garnet/lib/debug_ipc/helper/zx_status.h"
#include "lib/fxl/logging.h"

namespace debug_agent {

DebuggedJob::DebuggedJob(ProcessStartHandler* handler, zx_koid_t job_koid,
                         zx::job job)
    : handler_(handler), koid_(job_koid), job_(std::move(job)) {}

DebuggedJob::~DebuggedJob() = default;

zx_status_t DebuggedJob::Init() {
  debug_ipc::MessageLoopTarget* loop = debug_ipc::MessageLoopTarget::Current();
  FXL_DCHECK(loop);  // Loop must be created on this thread first.

  // Register for debug exceptions.
  debug_ipc::MessageLoopTarget::WatchJobConfig config;
  config.job_name = NameForObject(job_);
  config.job_handle = job_.get();
  config.job_koid = koid_;
  config.watcher = this;
  return loop->WatchJobExceptions(std::move(config), &job_watch_handle_);
}

void DebuggedJob::OnProcessStarting(zx_koid_t job_koid, zx_koid_t process_koid,
                                    zx_koid_t thread_koid) {
  FXL_DCHECK(job_koid == koid_);

  zx::process process = GetProcessFromKoid(process_koid);
  auto proc_name = NameForObject(process);
  zx::thread thread = ThreadForKoid(process.get(), thread_koid);

  // Search through the available filters. If the regex is not valid, fallback
  // to checking if |proc_name| contains the filter.
  FilterInfo* matching_filter = nullptr;
  for (auto& filter : filters_) {
    if (filter.regex.valid()) {
      if (filter.regex.Match(proc_name)) {
        matching_filter = &filter;
        break;
      }
    } else {
      // TODO(DX-953): Job filters should always be valid.
      if (proc_name.find(filter.filter) != std::string::npos) {
        matching_filter = &filter;
        break;
      }
    }
  }

  if (matching_filter) {
    DEBUG_LOG() << "Filter " << matching_filter->filter << " matches process "
                << proc_name << ". Attaching.";
    handler_->OnProcessStart(std::move(process));
  }

  // Attached to the process. At that point it will get a new thread
  // notification for the initial thread which it can stop or continue as it
  // desires. Therefore, we can always resume the thread in the "new process"
  // exception.
  debug_ipc::MessageLoopTarget::Current()->ResumeFromException(thread_koid,
                                                               thread, 0);
}

void DebuggedJob::SetFilters(std::vector<std::string> filters) {
  filters_.clear();
  filters_.reserve(filters.size());

  for (auto& filter : filters) {
    debug_ipc::Regex regex;
    if (!regex.Init(filter)) {
      FXL_LOG(WARNING) << "Could not initialize regex for filter " << filter;
    }

    FilterInfo filter_info = {};
    filter_info.filter = std::move(filter);
    filter_info.regex = std::move(regex);

    filters_.push_back(std::move(filter_info));
  }
}

void DebuggedJob::AppendFilter(std::string filter) {
  // We check whether this filter already exists.
  for (auto& existent_filter : filters_) {
    if (existent_filter.filter == filter)
      return;
  }

    debug_ipc::Regex regex;
    if (!regex.Init(filter)) {
      FXL_LOG(WARNING) << "Could not initialize regex for filter " << filter;
    }

    FilterInfo filter_info = {};
    filter_info.filter = std::move(filter);
    filter_info.regex = std::move(regex);

  filters_.push_back(std::move(filter_info));
}

}  // namespace debug_agent
