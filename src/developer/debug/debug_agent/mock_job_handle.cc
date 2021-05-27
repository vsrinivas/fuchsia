// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_job_handle.h"

#include "src/developer/debug/debug_agent/mock_process_handle.h"

namespace debug_agent {

MockJobHandle::MockJobHandle(zx_koid_t koid, std::string name)
    : job_koid_(koid), name_(std::move(name)) {}

std::unique_ptr<JobHandle> MockJobHandle::Duplicate() const {
  return std::make_unique<MockJobHandle>(*this);
}

std::vector<std::unique_ptr<JobHandle>> MockJobHandle::GetChildJobs() const {
  // Need to return a unique set of objects every time so make copies.
  std::vector<std::unique_ptr<JobHandle>> result;
  for (auto& job : child_jobs_)
    result.push_back(std::make_unique<MockJobHandle>(job));
  return result;
}

std::vector<std::unique_ptr<ProcessHandle>> MockJobHandle::GetChildProcesses() const {
  // Need to return a unique set of objects every time so make copies.
  std::vector<std::unique_ptr<ProcessHandle>> result;
  for (auto& process : child_processes_)
    result.push_back(std::make_unique<MockProcessHandle>(process));
  return result;
}

}  // namespace debug_agent
