// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_JOB_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_JOB_HANDLE_H_

#include "src/developer/debug/debug_agent/job_handle.h"
#include "src/developer/debug/debug_agent/mock_process_handle.h"

namespace debug_agent {

class MockJobHandle final : public JobHandle {
 public:
  MockJobHandle(zx_koid_t koid, std::string name = std::string());

  // Sets the child jobs and processes. These will be copied since we need to return a new
  // unique_ptr for each call to GetChildJobs() / GetChildProcesses().
  void set_child_jobs(std::vector<MockJobHandle> jobs) { child_jobs_ = std::move(jobs); }
  void set_child_processes(std::vector<MockProcessHandle> processes) {
    child_processes_ = std::move(processes);
  }

  // JobHandle implementation.
  std::unique_ptr<JobHandle> Duplicate() const override;
  zx_koid_t GetKoid() const override { return job_koid_; }
  std::string GetName() const override { return name_; }
  std::vector<std::unique_ptr<JobHandle>> GetChildJobs() const override;
  std::vector<std::unique_ptr<ProcessHandle>> GetChildProcesses() const override;
  debug::Status WatchJobExceptions(
      fit::function<void(std::unique_ptr<ProcessHandle>)> cb) override {
    return debug::Status("Mock doesn't implement watching job exceptions.");
  }

 private:
  zx_koid_t job_koid_;
  std::string name_;

  std::vector<MockJobHandle> child_jobs_;
  std::vector<MockProcessHandle> child_processes_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_JOB_HANDLE_H_
