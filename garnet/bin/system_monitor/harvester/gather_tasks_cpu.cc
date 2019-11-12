// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <string>

#include <task-utils/walker.h>

#include "gather_tasks.h"
#include "harvester.h"
#include "src/lib/fxl/logging.h"

// This is a short term workaround/hack. The code is separated form the
// "gather_tasks" code because getting memory information about tasks from the
// kernel is very heavy. When that becomes a lightweight operation, this code
// can be merged with "gather_tasks" or removed. Note that in the meantime, both
// "gather_tasks" and this code will collect tasks/thread CPU data (and that
// should should be fine).

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
  void Gather() {
    Clear();
    WalkRootJobTree();
  }

  // Clear all jobs/processes/threads information. Note that this is called by
  // Gather() and the destructor (i.e. no need for a separate call to Clear()
  // for those cases).
  void Clear() {
    // It may be worth checking if this can be  optimized by sending the handles
    // in batches.

    for (auto& job : jobs_) {
      zx_handle_close(job.handle);
    }
    jobs_.clear();

    for (auto& process : processes_) {
      zx_handle_close(process.handle);
    }
    processes_.clear();

    for (auto& thread : threads_) {
      zx_handle_close(thread.handle);
    }
    threads_.clear();
  }

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

namespace {

class UploadTaskSamples final {
 public:
  UploadTaskSamples() = default;

  // After gathering the data, upload it to |dockyard|.
  void UploadTaskInfo(DockyardProxy* dockyard_proxy) {
    if (FXL_VLOG_IS_ON(1)) {
      for (const auto& int_sample : int_sample_list_) {
        FXL_VLOG(1) << int_sample.first << ": " << int_sample.second;
      }
      for (const auto& string_sample : string_sample_list_) {
        FXL_VLOG(1) << string_sample.first << ": " << string_sample.second;
      }
    }

    dockyard_proxy->SendSampleList(int_sample_list_);
    dockyard_proxy->SendStringSampleList(string_sample_list_);

    int_sample_list_.clear();
    string_sample_list_.clear();
  }

  // Gather stats for a specific job.
  // |koid| must refer to the same job as the job handle.
  void AddJobStats(zx_handle_t job, zx_koid_t koid) {
    zx_info_job_t info;
    zx_status_t status =
        zx_object_get_info(job, ZX_INFO_JOB, &info, sizeof(info),
                           /*actual=*/nullptr, /*avail=*/nullptr);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "AddJobStats failed for koid " << koid << " ("
                       << status << ")" << zx_status_get_string(status);
      return;
    }
    AddKoidValue(koid, "kill_on_oom", info.kill_on_oom);
  }

  // Helper to add a value to the sample |int_sample_list_|.
  void AddKoidValue(zx_koid_t koid, const std::string& path,
                    dockyard::SampleValue value) {
    std::ostringstream label;
    label << "koid:" << koid << ":" << path;
    int_sample_list_.emplace_back(label.str(), value);
  }

  // Helper to add a value to the string list.
  void AddKoidString(zx_koid_t koid, const std::string& path,
                     const std::string& value) {
    std::ostringstream label;
    label << "koid:" << koid << ":" << path;
    string_sample_list_.emplace_back(label.str(), value);
  }

  // Helper to add the name of a koid to the string list.
  // |koid| must refer to the same task as the task handle.
  void AddKoidName(zx_handle_t task, zx_koid_t koid) {
    char name[ZX_MAX_NAME_LEN];
    zx_status_t status =
        zx_object_get_property(task, ZX_PROP_NAME, &name, sizeof(name));
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "AddKoidName failed for koid " << koid << " ("
                       << status << ")" << zx_status_get_string(status);
      return;
    }
    AddKoidString(koid, "name", name);
    FXL_VLOG(1) << "name " << name;
  }

  // Gather state info for a specific thread.
  // |koid| must refer to the same thread as the thread handle.
  void AddThreadState(zx_handle_t thread, zx_koid_t koid) {
    zx_info_thread_t info;
    zx_status_t status =
        zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info),
                           /*actual=*/nullptr, /*avail=*/nullptr);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "AddThreadStats failed for koid " << koid << " ("
                       << status << ")" << zx_status_get_string(status);
      return;
    }
    AddKoidValue(koid, "thread_state", info.state);
  }

  // Gather cpu info for a specific thread.
  // |koid| must refer to the same thread as the thread handle.
  void AddThreadCpu(zx_handle_t thread, zx_koid_t koid) {
    zx_info_thread_stats_t stats;
    zx_status_t status =
        zx_object_get_info(thread, ZX_INFO_THREAD_STATS, &stats, sizeof(stats),
                           /*actual=*/nullptr, /*avail=*/nullptr);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "AddThreadStats failed for koid " << koid << " ("
                       << status << ") " << zx_status_get_string(status);
      return;
    }
    AddKoidValue(koid, "cpu_total", stats.total_runtime);
  }

 private:
  SampleList int_sample_list_;
  StringSampleList string_sample_list_;
};

void UploadBasics(const std::vector<TaskTree::Task>& tasks,
                  dockyard::KoidType type, DockyardProxy* dockyard_proxy) {
  UploadTaskSamples upload;
  for (const auto& task : tasks) {
    upload.AddKoidValue(task.koid, "type", type);
    upload.AddKoidValue(task.koid, "parent_koid", task.parent_koid);
    upload.AddKoidName(task.handle, task.koid);
  }
  upload.UploadTaskInfo(dockyard_proxy);
}

void UploadThreadCpu(const std::vector<TaskTree::Task>& tasks,
                     DockyardProxy* dockyard_proxy) {
  UploadTaskSamples upload;
  for (const auto& task : tasks) {
    upload.AddThreadCpu(task.handle, task.koid);
  }
  upload.UploadTaskInfo(dockyard_proxy);
}

}  // namespace

GatherTasksCpu::GatherTasksCpu(zx_handle_t root_resource,
                               harvester::DockyardProxy* dockyard_proxy)
    : GatherCategory(root_resource, dockyard_proxy), task_tree_(new TaskTree) {}

GatherTasksCpu::~GatherTasksCpu() { delete task_tree_; }

void GatherTasksCpu::Gather() {
  if (actions_.WantRefresh()) {
    task_tree_->Gather();
    UploadBasics(task_tree_->Jobs(), dockyard::KoidType::JOB, DockyardPtr());
    UploadBasics(task_tree_->Processes(), dockyard::KoidType::PROCESS,
                 DockyardPtr());
    UploadBasics(task_tree_->Threads(), dockyard::KoidType::THREAD,
                 DockyardPtr());
  }
  if (actions_.WantThreadCpuSamples()) {
    UploadThreadCpu(task_tree_->Threads(), DockyardPtr());
  }
  actions_.NextInterval();
}

}  // namespace harvester
