// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_TASK_TREE_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_TASK_TREE_H_

#include <zircon/status.h>

#include <task-utils/walker.h>

#include "gather_category.h"

namespace harvester {

class TaskTree final : public TaskEnumerator {
 public:
  struct Task {
    Task(zx_handle_t handle, zx_koid_t koid, zx_koid_t parent_koid)
        : handle(handle), koid(koid), parent_koid(parent_koid) {}
    zx_handle_t handle;
    zx_koid_t koid;
    zx_koid_t parent_koid;
  };

  TaskTree() = default;

  ~TaskTree() override { Clear(); }

  // Collect a new set of tasks (jobs/processes/threads). Note that this will
  // clear out any prior task information.
  void Gather();

  // Clear all jobs/processes/threads information. Note that this is called by
  // Gather() and the destructor (i.e. no need for a separate call to Clear()
  // for those cases).
  void Clear();

  // Accessors to immutable lists of jobs/processes/threads.
  const std::vector<Task>& Jobs() const { return jobs_; }
  const std::vector<Task>& Processes() const { return processes_; }
  const std::vector<Task>& Threads() const { return threads_; }

 private:
  std::vector<Task> jobs_;
  std::vector<Task> processes_;
  std::vector<Task> threads_;

  // |TaskEnumerator| Callback for a job.
  zx_status_t OnJob(int /*depth*/, zx_handle_t job, zx_koid_t koid,
                    zx_koid_t parent_koid) override {
    zx_handle_t handle;
    zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &handle);
    jobs_.emplace_back(handle, koid, parent_koid);
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a process.
  zx_status_t OnProcess(int /*depth*/, zx_handle_t process, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    zx_handle_t handle;
    zx_handle_duplicate(process, ZX_RIGHT_SAME_RIGHTS, &handle);
    processes_.emplace_back(handle, koid, parent_koid);
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a thread.
  zx_status_t OnThread(int /*depth*/, zx_handle_t thread, zx_koid_t koid,
                       zx_koid_t parent_koid) override {
    zx_handle_t handle;
    zx_handle_duplicate(thread, ZX_RIGHT_SAME_RIGHTS, &handle);
    threads_.emplace_back(handle, koid, parent_koid);
    return ZX_OK;
  }

  // |TaskEnumerator| Enable On*() calls.
  bool has_on_job() const final { return true; }
  bool has_on_process() const final { return true; }
  bool has_on_thread() const final { return true; }
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_TASK_TREE_H_
