// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_tree.h"

#include <fuchsia/kernel/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include "os.h"

namespace harvester {

const size_t kNumInitialKoids = 128;

zx_status_t TaskTree::GetHandleForChildKoid(zx_koid_t child_koid,
                                            zx_handle_t parent,
                                            zx_koid_t parent_koid,
                                            zx_handle_t* child_handle) {
  auto it = koids_to_handles_.find(child_koid);

  if (it != koids_to_handles_.end()) {
    *child_handle = it->second;
    stale_koids_to_handles_.erase(child_koid);
    return ZX_OK;
  }

  zx_status_t status = zx_object_get_child(parent, child_koid,
                                           ZX_RIGHT_SAME_RIGHTS, child_handle);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "zx_object_get_child(" << parent_koid << ", (job)"
                     << child_koid
                     << ", ...) failed: " << zx_status_get_string(status)
                     << " (" << status << ")";
  } else {
    koids_to_handles_.insert(
        std::pair<zx_koid_t, zx_handle_t>(child_koid, *child_handle));
    stale_koids_to_handles_.erase(child_koid);
  }

  return status;
}

void TaskTree::GatherThreadsForProcess(zx_handle_t parent_process,
                                       zx_koid_t parent_process_koid) {
  zx_status_t status;

  // Get the koids for the threads belonging to this process.
  std::vector<zx_koid_t> koids(kNumInitialKoids);

  status = os_->GetChildren(parent_process, parent_process_koid,
                          ZX_INFO_PROCESS_THREADS, "ZX_INFO_PROCESS_THREADS",
                          koids);
  if (status != ZX_OK) {
    return;
  }

  for (zx_koid_t koid : koids) {
    zx_handle_t next_thread_handle;

    status = GetHandleForChildKoid(koid, parent_process, parent_process_koid,
                                   &next_thread_handle);

    if (status == ZX_OK) {
      // Store the thread / koid / parent process triple.
      threads_.emplace_back(next_thread_handle, koid, parent_process_koid);
    }
  }
}

void TaskTree::GatherProcessesForJob(zx_handle_t parent_job,
                                     zx_koid_t parent_job_koid) {
  zx_status_t status;

  // Get the koids for the processes under this job.
  std::vector<zx_koid_t> koids(kNumInitialKoids);

  status = os_->GetChildren(parent_job, parent_job_koid, ZX_INFO_JOB_PROCESSES,
                          "ZX_INFO_JOB_PROCESSES", koids);
  if (status != ZX_OK) {
    return;
  }

  for (zx_koid_t koid : koids) {
    zx_handle_t next_process_handle;

    status = GetHandleForChildKoid(koid, parent_job, parent_job_koid,
                                   &next_process_handle);

    if (status == ZX_OK) {
      // Store the process / koid / parent job triple.
      processes_.emplace_back(next_process_handle, koid, parent_job_koid);

      // Gather the process's threads.
      GatherThreadsForProcess(next_process_handle, koid);
    }
  }
}

void TaskTree::GatherProcessesAndJobsForJob(zx_handle_t parent_job,
                                            zx_koid_t parent_job_koid) {
  zx_status_t status;

  // Gather the job's processes.
  GatherProcessesForJob(parent_job, parent_job_koid);

  // Get the koids for the child jobs under this job.
  std::vector<zx_koid_t> koids(kNumInitialKoids);

  status = os_->GetChildren(parent_job, parent_job_koid, ZX_INFO_JOB_CHILDREN,
                          "ZX_INFO_JOB_CHILDREN", koids);
  if (status != ZX_OK) {
    return;
  }

  for (zx_koid_t koid : koids) {
    zx_handle_t child_job_handle;

    status = GetHandleForChildKoid(koid, parent_job, parent_job_koid,
                                   &child_job_handle);

    if (status == ZX_OK) {
      // Store the child job / koid / parent job triple.
      jobs_.emplace_back(child_job_handle, koid, parent_job_koid);

      // Gather the job's processes and child jobs.
      GatherProcessesAndJobsForJob(child_job_handle, koid);
    }
  }
}

void TaskTree::GatherJobs() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not create channel";
    return;
  }

  status =
      fdio_service_connect("/svc/fuchsia.kernel.RootJob", remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot open fuchsia.kernel.RootJob: "
                   << zx_status_get_string(status);
    return;
  }

  zx_handle_t root_job;
  zx_koid_t root_job_koid = 0;
  auto it = koids_to_handles_.find(root_job_koid);

  if (it != koids_to_handles_.end()) {
    root_job = it->second;
  } else {
    zx_status_t fidl_status = fuchsia_kernel_RootJobGet(local.get(), &root_job);
    if (fidl_status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot obtain root job";
      return;
    }

    koids_to_handles_.insert(
        std::pair<zx_koid_t, zx_handle_t>(root_job_koid, root_job));
  }

  // We will rebuild these data structures as we walk the job tree.
  jobs_.clear();
  processes_.clear();
  threads_.clear();

  stale_koids_to_handles_.insert(koids_to_handles_.begin(),
                                 koids_to_handles_.end());
  stale_koids_to_handles_.erase(root_job_koid);

  // Store the root job node.
  jobs_.emplace_back(root_job, root_job_koid, root_job_koid);

  // Gather the root job's processes and jobs.
  GatherProcessesAndJobsForJob(root_job, root_job_koid);
  for (auto& [koid, handle] : stale_koids_to_handles_) {
    koids_to_handles_.erase(koid);
    zx_handle_close(handle);
  }
  stale_koids_to_handles_.clear();
}

void TaskTree::Gather() { GatherJobs(); }

void TaskTree::Clear() {
  // Close all open handles.
  for (auto& koid_handle_pair : koids_to_handles_) {
    zx_handle_close(koid_handle_pair.second);
  }
  koids_to_handles_.clear();

  jobs_.clear();
  processes_.clear();
  threads_.clear();
}

}  // namespace harvester
