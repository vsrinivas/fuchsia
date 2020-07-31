// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_JOB_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_JOB_HANDLE_H_

#include <lib/zx/job.h>

#include "src/developer/debug/debug_agent/job_handle.h"

namespace debug_agent {

class ZirconJobHandle final : public JobHandle {
 public:
  explicit ZirconJobHandle(zx::job j);
  ZirconJobHandle(const ZirconJobHandle& other);
  ZirconJobHandle(ZirconJobHandle&& other) = default;

  // JobHandle implementation.
  std::unique_ptr<JobHandle> Duplicate() const override;
  const zx::job& GetNativeHandle() const override { return job_; }
  zx::job& GetNativeHandle() override { return job_; }
  zx_koid_t GetKoid() const override { return job_koid_; }
  std::string GetName() const override;
  std::vector<std::unique_ptr<JobHandle>> GetChildJobs() const override;
  std::vector<std::unique_ptr<ProcessHandle>> GetChildProcesses() const override;

 private:
  zx_koid_t job_koid_;
  zx::job job_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_JOB_HANDLE_H_
