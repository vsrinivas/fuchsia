// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_tree.h"

#include <fuchsia/boot/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

namespace harvester {

const size_t kNumInitialKoids = 128;
const size_t kNumExtraKoids = 10;

zx_status_t TaskTree::GatherChildKoids(zx_handle_t parent,
                                       zx_koid_t parent_koid, int children_kind,
                                       const char* kind_name,
                                       std::vector<zx_koid_t>& child_koids) {
  size_t actual = 0;
  size_t available = 0;
  zx_status_t status;
  int count = 0;

  // This is inherently racy. Retry once with a bit of slop to try to
  // get a complete list.
  do {
    status = zx_object_get_info(parent, children_kind, child_koids.data(),
                                child_koids.size() * sizeof(zx_koid_t), &actual,
                                &available);
    if (status != ZX_OK) {
      fprintf(stderr,
              "ERROR: zx_object_get_info(%" PRIu64
              ", %s, ...) failed: %s (%d)\n",
              parent_koid, kind_name, zx_status_get_string(status), status);
      return status;
    }

    if (actual < available) {
      child_koids.resize(available + kNumExtraKoids);
    }

  } while (actual < available && count++ < 2);

  // If we're still too small at least warn the user.
  if (actual < available) {
    fprintf(stderr,
            "WARNING: zx_object_get_info(%" PRIu64
            ", %s, ...) truncated %zu/%zu results\n",
            parent_koid, kind_name, available - actual, available);
  }

  child_koids.resize(actual);

  return ZX_OK;
}

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
    fprintf(stderr,
            "WARNING: zx_object_get_child(%" PRIu64
            ", (job)%zu, ...) failed: %s (%d)\n",
            parent_koid, child_koid, zx_status_get_string(status), status);
  } else {
    koids_to_handles_.insert(
        std::pair<zx_koid_t, zx_handle_t>(child_koid, *child_handle));
    stale_koids_to_handles_.erase(child_koid);
  }

  return status;
}

zx_status_t TaskTree::GatherThreadsForProcess(zx_handle_t parent_process,
                                              zx_koid_t parent_process_koid) {
  zx_status_t status;

  // Get the koids for the threads belonging to this process.
  std::vector<zx_koid_t> koids(kNumInitialKoids);

  status = GatherChildKoids(parent_process, parent_process_koid,
                            ZX_INFO_PROCESS_THREADS, "ZX_INFO_PROCESS_THREADS",
                            koids);

  for (auto next_thread_koid = begin(koids);
       status == ZX_OK && next_thread_koid != end(koids); next_thread_koid++) {
    zx_handle_t next_thread_handle;

    status = GetHandleForChildKoid(*next_thread_koid, parent_process,
                                   parent_process_koid, &next_thread_handle);

    if (status == ZX_OK) {
      // Store the thread / koid / parent process triple.
      threads_.emplace_back(next_thread_handle, *next_thread_koid,
                            parent_process_koid);
    }
  }

  return status;
}

zx_status_t TaskTree::GatherProcessesForJob(zx_handle_t parent_job,
                                            zx_koid_t parent_job_koid) {
  zx_status_t status;

  // Get the koids for the processes under this job.
  std::vector<zx_koid_t> koids(kNumInitialKoids);

  status = GatherChildKoids(parent_job, parent_job_koid, ZX_INFO_JOB_PROCESSES,
                            "ZX_INFO_JOB_PROCESSES", koids);

  for (auto next_process_koid = begin(koids);
       status == ZX_OK && next_process_koid != end(koids);
       next_process_koid++) {
    zx_handle_t next_process_handle;

    status = GetHandleForChildKoid(*next_process_koid, parent_job,
                                   parent_job_koid, &next_process_handle);

    if (status == ZX_OK) {
      // Store the process / koid / parent job triple.
      processes_.emplace_back(next_process_handle, *next_process_koid,
                              parent_job_koid);

      // Gather the process's threads.
      status = GatherThreadsForProcess(next_process_handle, *next_process_koid);
    }
  }

  return status;
}

zx_status_t TaskTree::GatherProcessesAndJobsForJob(zx_handle_t parent_job,
                                                   zx_koid_t parent_job_koid) {
  zx_status_t status;

  // Gather the job's processes.
  status = GatherProcessesForJob(parent_job, parent_job_koid);
  if (status != ZX_OK) {
    return status;
  }

  // Get the koids for the child jobs under this job.
  std::vector<zx_koid_t> koids(kNumInitialKoids);

  status = GatherChildKoids(parent_job, parent_job_koid, ZX_INFO_JOB_CHILDREN,
                            "ZX_INFO_JOB_CHILDREN", koids);

  for (auto next_child_job_koid = begin(koids);
       status == ZX_OK && next_child_job_koid != end(koids);
       next_child_job_koid++) {
    zx_handle_t child_job_handle;

    status = GetHandleForChildKoid(*next_child_job_koid, parent_job,
                                   parent_job_koid, &child_job_handle);

    if (status == ZX_OK) {
      // Store the child job / koid / parent job triple.
      jobs_.emplace_back(child_job_handle, *next_child_job_koid,
                         parent_job_koid);

      // Gather the job's processes and child jobs.
      status =
          GatherProcessesAndJobsForJob(child_job_handle, *next_child_job_koid);
    }
  }

  return status;
}

zx_status_t TaskTree::GatherJobs() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect("/svc/fuchsia.boot.RootJob", remote.release());
  if (status != ZX_OK) {
    fprintf(stderr,
            "harvester/task_tree.cc: cannot open fuchsia.boot.RootJob: %s\n",
            zx_status_get_string(status));
    return status;
  }

  zx_handle_t root_job;
  zx_koid_t root_job_koid = 0;
  auto it = koids_to_handles_.find(root_job_koid);

  if (it != koids_to_handles_.end()) {
    root_job = it->second;
  } else {
    zx_status_t fidl_status = fuchsia_boot_RootJobGet(local.get(), &root_job);

    if (fidl_status != ZX_OK) {
      fprintf(stderr, "harvester/task_tree.cc: cannot obtain root job\n");
      return ZX_ERR_NOT_FOUND;
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
  status = GatherProcessesAndJobsForJob(root_job, root_job_koid);
  if (status == ZX_OK) {
    for (auto& [koid, handle] : stale_koids_to_handles_) {
      koids_to_handles_.erase(koid);
      zx_handle_close(handle);
    }
  }
  stale_koids_to_handles_.clear();

  return status;
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
