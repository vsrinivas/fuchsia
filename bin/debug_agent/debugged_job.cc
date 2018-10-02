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
  zx::thread thread = ThreadForKoid(process.get(), thread_koid);

  // TODO(anmittal): Add filter and then only call OnProcessStart.
  handler_->OnProcessStart(std::move(process));

  debug_ipc::MessageLoopZircon::Current()->ResumeFromException(thread, 0);
}

}  // namespace debug_agent
