// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_tasks.h"

#include <zircon/status.h>

#include <array>
#include <string>

#include <task-utils/walker.h>

#include "harvester.h"
#include "sample_bundle.h"
#include "src/lib/fxl/logging.h"
#include "task_tree.h"

namespace harvester {

namespace {

// Utilities to create a SampleBundle with task stats.
class SampleBundleBuilder final {
 public:
  explicit SampleBundleBuilder(SampleBundle* samples)
      : sample_bundle_(samples) {}

  // Gather stats for a specific job.
  // |koid| must refer to the same job as the job handle.
  void AddJobStats(zx_handle_t job, zx_koid_t koid);

  // Helper to add a value to the sample |int_sample_list_|.
  void AddKoidValue(zx_koid_t koid, const std::string& path,
                    dockyard::SampleValue value);

  // Helper to add a value to the string list.
  void AddKoidString(zx_koid_t koid, const std::string& path,
                     const std::string& value);

  // Helper to add the name of a koid to the string list.
  // |koid| must refer to the same task as the task handle.
  void AddKoidName(zx_handle_t task, zx_koid_t koid);

  // Gather stats for a specific process.
  // |koid| must refer to the same process as the process handle.
  void AddProcessStats(zx_handle_t process, zx_koid_t koid);

  // Gather state info for a specific thread.
  // |koid| must refer to the same thread as the thread handle.
  void AddThreadState(zx_handle_t thread, zx_koid_t koid);

  // Gather cpu info for a specific thread.
  // |koid| must refer to the same thread as the thread handle.
  void AddThreadCpu(zx_handle_t thread, zx_koid_t koid);

 private:
  SampleBundle* sample_bundle_;

  SampleBundleBuilder() = delete;
};

// Gather stats for a specific job.
// |koid| must refer to the same job as the job handle.
void SampleBundleBuilder::AddJobStats(zx_handle_t job, zx_koid_t koid) {
  zx_info_job_t info;
  zx_status_t status =
      zx_object_get_info(job, ZX_INFO_JOB, &info, sizeof(info),
                         /*actual=*/nullptr, /*avail=*/nullptr);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << ZxErrorString("AddJobStats", status);
    return;
  }
  AddKoidValue(koid, "kill_on_oom", info.kill_on_oom);
}

// Helper to add a value to the sample |int_sample_list_|.
void SampleBundleBuilder::AddKoidValue(zx_koid_t koid, const std::string& path,
                                       dockyard::SampleValue value) {
  sample_bundle_->AddIntSample("koid", koid, path, value);
}

// Helper to add a value to the string list.
void SampleBundleBuilder::AddKoidString(zx_koid_t koid, const std::string& path,
                                        const std::string& value) {
  sample_bundle_->AddStringSample("koid", koid, path, value);
}

// Helper to add the name of a koid to the string list.
// |koid| must refer to the same task as the task handle.
void SampleBundleBuilder::AddKoidName(zx_handle_t task, zx_koid_t koid) {
  std::array<char, ZX_MAX_NAME_LEN> name;
  zx_status_t status =
      zx_object_get_property(task, ZX_PROP_NAME, &name, name.size());
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << ZxErrorString("AddKoidName", status);
    return;
  }
  AddKoidString(koid, "name", name.data());
  FXL_VLOG(2) << "name " << name.data();
}

// Gather stats for a specific process.
// |koid| must refer to the same process as the process handle.
void SampleBundleBuilder::AddProcessStats(zx_handle_t process, zx_koid_t koid) {
  zx_info_task_stats_t info;
  zx_status_t status =
      zx_object_get_info(process, ZX_INFO_TASK_STATS, &info, sizeof(info),
                         /*actual=*/nullptr, /*avail=*/nullptr);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << ZxErrorString("AddProcessStats", status);
    return;
  }
  AddKoidValue(koid, "memory_mapped_bytes", info.mem_mapped_bytes);
  AddKoidValue(koid, "memory_private_bytes", info.mem_private_bytes);
  AddKoidValue(koid, "memory_shared_bytes", info.mem_shared_bytes);
  AddKoidValue(koid, "memory_scaled_shared_bytes",
               info.mem_scaled_shared_bytes);
}

// Gather state info for a specific thread.
// |koid| must refer to the same thread as the thread handle.
void SampleBundleBuilder::AddThreadState(zx_handle_t thread, zx_koid_t koid) {
  zx_info_thread_t info;
  zx_status_t status =
      zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info),
                         /*actual=*/nullptr, /*avail=*/nullptr);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << ZxErrorString("AddThreadState", status);
    return;
  }
  AddKoidValue(koid, "thread_state", info.state);
}

// Gather cpu info for a specific thread.
// |koid| must refer to the same thread as the thread handle.
void SampleBundleBuilder::AddThreadCpu(zx_handle_t thread, zx_koid_t koid) {
  zx_info_thread_stats_t stats;
  zx_status_t status =
      zx_object_get_info(thread, ZX_INFO_THREAD_STATS, &stats, sizeof(stats),
                         /*actual=*/nullptr, /*avail=*/nullptr);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << ZxErrorString("AddThreadCpu", status);
    return;
  }
  AddKoidValue(koid, "cpu_total", stats.total_runtime);
}

}  // namespace

void AddTaskBasics(SampleBundle* samples,
                   const std::vector<TaskTree::Task>& tasks,
                   dockyard::KoidType type) {
  SampleBundleBuilder builder(samples);
  for (const auto& task : tasks) {
    builder.AddKoidValue(task.koid, "type", type);
    builder.AddKoidValue(task.koid, "parent_koid", task.parent_koid);
    builder.AddKoidName(task.handle, task.koid);
  }
}

void AddJobStats(SampleBundle* samples,
                 const std::vector<TaskTree::Task>& tasks) {
  SampleBundleBuilder builder(samples);
  for (const auto& task : tasks) {
    builder.AddJobStats(task.handle, task.koid);
  }
}

void AddProcessStats(SampleBundle* samples,
                     const std::vector<TaskTree::Task>& tasks) {
  SampleBundleBuilder builder(samples);
  for (const auto& task : tasks) {
    builder.AddProcessStats(task.handle, task.koid);
  }
}

void AddThreadStats(SampleBundle* samples,
                    const std::vector<TaskTree::Task>& tasks) {
  SampleBundleBuilder builder(samples);
  for (const auto& task : tasks) {
    builder.AddThreadState(task.handle, task.koid);
    builder.AddThreadCpu(task.handle, task.koid);
  }
}

void GatherTasks::Gather() {
  TaskTree task_tree;
  task_tree.Gather();
  SampleBundle samples;
  AddTaskBasics(&samples, task_tree.Jobs(), dockyard::KoidType::JOB);
  AddTaskBasics(&samples, task_tree.Processes(), dockyard::KoidType::PROCESS);
  AddTaskBasics(&samples, task_tree.Threads(), dockyard::KoidType::THREAD);

  AddJobStats(&samples, task_tree.Jobs());
  AddProcessStats(&samples, task_tree.Processes());
  AddThreadStats(&samples, task_tree.Threads());
  samples.Upload(DockyardPtr());
}

}  // namespace harvester
