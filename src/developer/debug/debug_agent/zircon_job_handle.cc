// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_job_handle.h"

#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"

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

}  // namespace debug_agent
