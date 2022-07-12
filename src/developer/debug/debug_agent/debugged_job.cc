// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_job.h"

#include <lib/syslog/cpp/macros.h>

namespace debug_agent {

DebuggedJob::DebuggedJob(ProcessStartHandler* handler, std::unique_ptr<JobHandle> job_handle)
    : handler_(handler), job_handle_(std::move(job_handle)) {}

DebuggedJob::~DebuggedJob() = default;

debug::Status DebuggedJob::Init() {
  // Register for debug exceptions. Since this class owks the job_handle_ it is safe to capture
  // |this| here.
  return job_handle_->WatchJobExceptions([this](std::unique_ptr<ProcessHandle> process) {
    handler_->OnProcessStart(std::move(process));
  });
}

}  // namespace debug_agent
