// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_TASK_TREE_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_TASK_TREE_H_

#include <zircon/types.h>

#include <map>
#include <vector>

namespace harvester {

class TaskTree {
 public:
  struct Task {
    Task(zx_handle_t handle, zx_koid_t koid, zx_koid_t parent_koid)
        : handle(handle), koid(koid), parent_koid(parent_koid) {}
    zx_handle_t handle;
    zx_koid_t koid;
    zx_koid_t parent_koid;
  };

  TaskTree() = default;

  ~TaskTree() { Clear(); }

  // Creates and stores handles to all tasks (threads, processes, and jobs)
  // descending from the root job. The first call to GatherJobs creates
  // a handle for each existing task. Subsequent calls create handles for
  // all new tasks, drawing from its cache of handles for existing tasks.
  void Gather();

  // Accessors to immutable lists of jobs/processes/threads.
  const std::vector<Task>& Jobs() const { return jobs_; }
  const std::vector<Task>& Processes() const { return processes_; }
  const std::vector<Task>& Threads() const { return threads_; }

 protected:
  std::vector<Task> jobs_;
  std::vector<Task> processes_;
  std::vector<Task> threads_;
  std::map<zx_koid_t, zx_handle_t> koids_to_handles_;
  std::map<zx_koid_t, zx_handle_t> stale_koids_to_handles_;

  // Clears all jobs/processes/threads information.
  void Clear();

  // Fills |child_koids| with the child koids of |parent|.
  zx_status_t GatherChildKoids(zx_handle_t parent, zx_koid_t parent_koid,
                               int children_kind, const char* kind_name,
                               std::vector<zx_koid_t>& child_koids);

  // Fills |child_handle| with a handle to |child_koid|, either from a cache or
  // newly-created.
  zx_status_t GetHandleForChildKoid(zx_koid_t child_koid, zx_handle_t parent,
                                    zx_koid_t parent_koid,
                                    zx_handle_t* child_handle);

  // Creates and stores handles to all threads belonging to |parent_process|.
  void GatherThreadsForProcess(zx_handle_t parent_process,
                               zx_koid_t parent_process_koid);

  // Creates and stores handles to all processes belonging to |parent_job|.
  void GatherProcessesForJob(zx_handle_t parent_job,
                             zx_koid_t parent_job_koid);

  // Creates and stores handles to all processes and jobs belonging to
  // |parent_job|.
  void GatherProcessesAndJobsForJob(zx_handle_t parent_job,
                                    zx_koid_t parent_job_koid);

  // Update the collection of known tasks (jobs/processes/threads).
  void GatherJobs();
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_TASK_TREE_H_
