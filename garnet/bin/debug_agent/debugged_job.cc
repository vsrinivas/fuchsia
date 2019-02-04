// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

#include "garnet/bin/debug_agent/debugged_job.h"
#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/bin/debug_agent/system_info.h"
#include "lib/fxl/logging.h"

namespace debug_agent {

DebuggedJob::DebuggedJob(ProcessStartHandler* handler, zx_koid_t job_koid,
                         zx::job job)
    : handler_(handler), koid_(job_koid), job_(std::move(job)) {}

DebuggedJob::~DebuggedJob() = default;

bool DebuggedJob::Init() {
  debug_ipc::MessageLoopZircon* loop = debug_ipc::MessageLoopZircon::Current();
  FXL_DCHECK(loop);  // Loop must be created on this thread first.

  // Register for debug exceptions.
  job_watch_handle_ = loop->WatchJobExceptions(job_.get(), koid_, this);
  return job_watch_handle_.watching();
}

void DebuggedJob::OnProcessStarting(zx_koid_t job_koid, zx_koid_t process_koid,
                                    zx_koid_t thread_koid) {
  FXL_DCHECK(job_koid == koid_);

  zx::process process = GetProcessFromKoid(process_koid);
  auto proc_name = NameForObject(process);
  zx::thread thread = ThreadForKoid(process.get(), thread_koid);

  bool found = false;
  // Search through the available filters. If the regex is not valid, fallback
  // to checking if |proc_name| contains the filter.
  for (auto& filter : filters_) {
    if (filter.regex.valid()) {
      if (filter.regex.Match(proc_name)) {
        found = true;
        break;
      }
    } else {
      // TODO(DX-953): Job filters should always be valid.
      if (proc_name.find(filter.filter) != std::string::npos) {
        found = true;
        break;
      }
    }
  }

  if (found)
    handler_->OnProcessStart(std::move(process));

  // Attached to the process. At that point it will get a new thread
  // notification for the initial thread which it can stop or continue as it
  // desires. Therefore, we can always resume the thread in the "new process"
  // exception.
  debug_ipc::MessageLoopZircon::Current()->ResumeFromException(thread, 0);
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
