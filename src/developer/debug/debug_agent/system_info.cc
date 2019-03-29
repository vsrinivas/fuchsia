// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/system_info.h"

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/debug_agent/object_util.h"

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

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_NOTREACHED();
    return zx::job();
  }

  zx_handle_t root_job;
  zx_status_t fidl_status =
      fuchsia_sysinfo_DeviceGetRootJob(channel.get(), &status, &root_job);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    FXL_NOTREACHED();
    return zx::job();
  }
  return zx::job(root_job);
}

debug_ipc::ProcessTreeRecord GetProcessTreeRecord(
    const zx::object_base& object, debug_ipc::ProcessTreeRecord::Type type) {
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

// Searches root job for a job with the given
// koid. If found, puts it in *out* and returns true.
bool FindJob(zx::job root_job, zx_koid_t search_for, zx::job* out) {
  if (KoidForObject(root_job) == search_for) {
    out->reset(root_job.release());
    return true;
  }

  auto child_jobs = GetChildJobs(root_job.get());
  for (auto& child_job : child_jobs) {
    if (FindJob(zx::job(child_job.release()), search_for, out))
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

zx::job GetJobFromKoid(zx_koid_t koid) {
  zx::job result;
  FindJob(GetRootJob(), koid, &result);
  return result;
}

}  // namespace debug_agent
