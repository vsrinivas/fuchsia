// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/system_info.h"

#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "src/developer/debug/debug_agent/object_util.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

namespace {

debug_ipc::ProcessTreeRecord GetProcessTreeRecord(ObjectProvider* provider,
                                                  const zx::object_base& object,
                                                  debug_ipc::ProcessTreeRecord::Type type) {
  debug_ipc::ProcessTreeRecord result;
  result.type = type;
  result.koid = provider->KoidForObject(object);
  result.name = provider->NameForObject(object);

  if (type == debug_ipc::ProcessTreeRecord::Type::kJob) {
    std::vector<zx::process> child_procs = provider->GetChildProcesses(object.get());
    std::vector<zx::job> child_jobs = provider->GetChildJobs(object.get());
    result.children.reserve(child_procs.size() + child_jobs.size());

    for (const auto& job : child_jobs) {
      result.children.push_back(
          GetProcessTreeRecord(provider, job, debug_ipc::ProcessTreeRecord::Type::kJob));
    }
    for (const auto& proc : child_procs) {
      result.children.push_back(
          GetProcessTreeRecord(provider, proc, debug_ipc::ProcessTreeRecord::Type::kProcess));
    }
  }
  return result;
}

}  // namespace

zx_status_t GetProcessTree(debug_ipc::ProcessTreeRecord* root) {
  ObjectProvider* provider = ObjectProvider::Get();
  *root = GetProcessTreeRecord(provider, provider->GetRootJob(),
                               debug_ipc::ProcessTreeRecord::Type::kJob);
  return ZX_OK;
}

}  // namespace debug_agent
