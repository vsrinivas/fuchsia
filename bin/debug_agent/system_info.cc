// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/system_info.h"

#include <fcntl.h>
#include <unistd.h>
#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zx/job.h>
#include <zx/process.h>

#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/public/lib/fxl/logging.h"

namespace debug_agent {

namespace {

// TODO(brettw) this is based on the code in Zircon's task-utils which uses
// this hack to get the root job handle. It will likely need to be updated
// when a better way to get the root job is found.
zx::job GetRootJob() {
  int fd = open("/dev/misc/sysinfo", O_RDWR);
  if (fd < 0) {
    FXL_NOTREACHED();
    return zx::job();
  }

  zx_handle_t root_job;
  size_t n = ioctl_sysinfo_get_root_job(fd, &root_job);
  close(fd);
  if (n != sizeof(root_job)) {
    FXL_NOTREACHED();
    return zx::job();
  }
  return zx::job(root_job);
}

debug_ipc::ProcessTreeRecord GetProcessTreeRecord(
    const zx::object_base& object,
    debug_ipc::ProcessTreeRecord::Type type) {
  debug_ipc::ProcessTreeRecord result;
  result.type = type;
  result.koid = KoidForObject(object);
  result.name = NameForObject(object);

  if (type == debug_ipc::ProcessTreeRecord::Type::kJob) {
    std::vector<zx::process> child_procs = GetChildProcesses(object.get());
    std::vector<zx::job> child_jobs = GetChildJobs(object.get());
    result.children.reserve(child_procs.size() + child_jobs.size());

    for (const auto& job : child_jobs) {
      result.children.push_back(
          GetProcessTreeRecord(job, debug_ipc::ProcessTreeRecord::Type::kJob));
    }
    for (const auto& proc : child_procs) {
      result.children.push_back(GetProcessTreeRecord(
          proc, debug_ipc::ProcessTreeRecord::Type::kProcess));
    }
  }
  return result;
}

// Searches the process tree rooted at "job" for a process with the given
// koid. If found, puts it in *out* and returns true.
bool FindProcess(const zx::job& job, zx_koid_t search_for, zx::process* out) {
  for (auto& proc : GetChildProcesses(job.get())) {
    if (KoidForObject(proc) == search_for) {
      *out = std::move(proc);
      return true;
    }
  }

  for (const auto& job : GetChildJobs(job.get())) {
    if (FindProcess(job, search_for, out))
      return true;
  }
  return false;
}

}  // namespace

zx_status_t GetProcessTree(debug_ipc::ProcessTreeRecord* root) {
  *root = GetProcessTreeRecord(GetRootJob(),
                               debug_ipc::ProcessTreeRecord::Type::kJob);
  return ZX_OK;
}

zx::process GetProcessFromKoid(zx_koid_t koid) {
  zx::process result;
  FindProcess(GetRootJob(), koid, &result);
  return result;
}

}  // namespace debug_agent
