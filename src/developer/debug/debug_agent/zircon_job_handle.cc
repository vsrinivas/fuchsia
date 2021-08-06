// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_job_handle.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"
#include "src/developer/debug/shared/message_loop_target.h"

namespace debug_agent {

ZirconJobHandle::ZirconJobHandle(zx::job j)
    : job_koid_(zircon::KoidForObject(j)), job_(std::move(j)) {}

ZirconJobHandle::ZirconJobHandle(const ZirconJobHandle& other) : job_koid_(other.job_koid_) {
  other.job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_);
}

std::unique_ptr<JobHandle> ZirconJobHandle::Duplicate() const {
  return std::make_unique<ZirconJobHandle>(*this);
}

std::string ZirconJobHandle::GetName() const { return zircon::NameForObject(job_); }

std::vector<std::unique_ptr<JobHandle>> ZirconJobHandle::GetChildJobs() const {
  std::vector<std::unique_ptr<JobHandle>> result;
  for (auto& zx_job : zircon::GetChildJobs(job_))
    result.push_back(std::make_unique<ZirconJobHandle>(std::move(zx_job)));
  return result;
}

std::vector<std::unique_ptr<ProcessHandle>> ZirconJobHandle::GetChildProcesses() const {
  std::vector<std::unique_ptr<ProcessHandle>> result;
  for (auto& zx_process : zircon::GetChildProcesses(job_))
    result.push_back(std::make_unique<ZirconProcessHandle>(std::move(zx_process)));
  return result;
}

debug::Status ZirconJobHandle::WatchJobExceptions(
    fit::function<void(std::unique_ptr<ProcessHandle>)> cb) {
  debug::Status status;

  if (!cb) {
    // Unregistering.
    job_watch_handle_.StopWatching();
  } else if (!process_callback_) {
    // Registering for the first time.
    debug::MessageLoopTarget* loop = debug::MessageLoopTarget::Current();
    FX_DCHECK(loop);  // Loop must be created on this thread first.

    debug::MessageLoopTarget::WatchJobConfig config;
    config.job_name = GetName();
    config.job_handle = job_.get();
    config.job_koid = job_koid_;
    config.watcher = this;
    status = debug::ZxStatus(loop->WatchJobExceptions(std::move(config), &job_watch_handle_));
  }

  process_callback_ = std::move(cb);
  return status;
}

void ZirconJobHandle::OnProcessStarting(zx::exception exception_token,
                                        zx_exception_info_t exception_info) {
  zx_handle_t zircon_handle = ZX_HANDLE_INVALID;
  zx_status_t status = zx_exception_get_process(exception_token.get(), &zircon_handle);
  FX_DCHECK(status == ZX_OK) << "Got: " << zx_status_get_string(status);

  process_callback_(std::make_unique<ZirconProcessHandle>(zx::process(zircon_handle)));

  // Attached to the process. At that point it will get a new thread notification for the initial
  // thread which it can stop or continue as it desires. Therefore, we can always resume the thread
  // in the "new process" exception.
  //
  // Technically it's not necessary to reset the handle, but being explicit here helps readability.
  exception_token.reset();
}

}  // namespace debug_agent
