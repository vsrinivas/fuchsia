// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

#include "garnet/bin/debug_agent/debugged_job.h"
#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/bin/debug_agent/system_info.h"
#include "lib/fxl/logging.h"

namespace debug_agent {
namespace {

bool StartsWithCaseInsensitive(std::string mainStr, std::string toMatch) {
  std::transform(mainStr.begin(), mainStr.end(), mainStr.begin(), ::tolower);
  std::transform(toMatch.begin(), toMatch.end(), toMatch.begin(), ::tolower);
  return mainStr.find(toMatch) == 0;
}

}  // namespace
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

  // TODO(anmittal): use some data structure(trie) to make it efficient.
  for (auto& filter : filters_) {
    if (StartsWithCaseInsensitive(proc_name, filter)) {
      handler_->OnProcessStart(std::move(process));
      break;
    }
  }

  debug_ipc::MessageLoopZircon::Current()->ResumeFromException(thread, 0);
}

void DebuggedJob::SetFilters(std::vector<std::string> filters) {
  filters_ = std::move(filters);
}

}  // namespace debug_agent
