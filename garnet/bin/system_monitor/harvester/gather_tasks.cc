// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_tasks.h"

#include <zircon/status.h>

#include <string>

#include <task-utils/walker.h>

#include "harvester.h"
#include "src/lib/fxl/logging.h"

namespace harvester {

namespace {

class TaskHarvester final : public TaskEnumerator {
 public:
  TaskHarvester() = default;

  // After gathering the data, upload it to |dockyard|.
  void UploadTaskInfo(DockyardProxy* dockyard_proxy) {
    if (FXL_VLOG_IS_ON(2)) {
      for (const auto& int_sample : int_sample_list_) {
        FXL_VLOG(2) << int_sample.first << ": " << int_sample.second;
      }
      for (const auto& string_sample : string_sample_list_) {
        FXL_VLOG(2) << string_sample.first << ": " << string_sample.second;
      }
    }

    DockyardProxyStatus status =
        dockyard_proxy->SendSampleList(int_sample_list_);
    if (status != DockyardProxyStatus::OK) {
      FXL_LOG(ERROR) << DockyardErrorString("SendSampleList", status)
                     << " Job/process/thread information will be missing";
    }
    status = dockyard_proxy->SendStringSampleList(string_sample_list_);
    if (status != DockyardProxyStatus::OK) {
      FXL_LOG(ERROR) << DockyardErrorString("SendStringSampleList", status)
                     << " Job/process/thread names will be missing";
    }

    int_sample_list_.clear();
    string_sample_list_.clear();
  }

 private:
  SampleList int_sample_list_;
  StringSampleList string_sample_list_;

  // Gather stats for a specific job.
  // |koid| must refer to the same job as the job handle.
  void AddJobStats(zx_handle_t job, zx_koid_t koid) {
    zx_info_job_t info;
    zx_status_t status =
        zx_object_get_info(job, ZX_INFO_JOB, &info, sizeof(info),
                           /*actual=*/nullptr, /*avail=*/nullptr);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "AddJobStats failed for koid " << koid << " ("
                       << status << ")";
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
                       << status << ")";
      return;
    }
    AddKoidString(koid, "name", name);
    FXL_VLOG(2) << "name " << name;
  }

  // Gather stats for a specific process.
  // |koid| must refer to the same process as the process handle.
  void AddProcessStats(zx_handle_t process, zx_koid_t koid) {
    zx_info_task_stats_t info;
    zx_status_t status =
        zx_object_get_info(process, ZX_INFO_TASK_STATS, &info, sizeof(info),
                           /*actual=*/nullptr, /*avail=*/nullptr);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "AddProcessStats failed for koid " << koid << " ("
                       << status << ")";
      return;
    }
    AddKoidValue(koid, "memory_mapped_bytes", info.mem_mapped_bytes);
    AddKoidValue(koid, "memory_private_bytes", info.mem_private_bytes);
    AddKoidValue(koid, "memory_shared_bytes", info.mem_shared_bytes);
    AddKoidValue(koid, "memory_scaled_shared_bytes",
                 info.mem_scaled_shared_bytes);
  }

  // Gather stats for a specific thread.
  // |koid| must refer to the same thread as the thread handle.
  void AddThreadStats(zx_handle_t thread, zx_koid_t koid) {
    {
      zx_info_thread_t info;
      zx_status_t status =
          zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info),
                             /*actual=*/nullptr, /*avail=*/nullptr);
      if (status != ZX_OK) {
        FXL_LOG(WARNING) << "AddThreadStats failed for koid " << koid << " ("
                         << status << ")";
        return;
      }
      AddKoidValue(koid, "thread_state", info.state);
    }

    {
      zx_info_thread_stats_t stats;
      zx_status_t status = zx_object_get_info(
          thread, ZX_INFO_THREAD_STATS, &stats, sizeof(stats),
          /*actual=*/nullptr, /*avail=*/nullptr);
      if (status != ZX_OK) {
        FXL_LOG(WARNING) << "AddThreadStats failed for koid " << koid << " ("
                         << status << ")";
        return;
      }
      AddKoidValue(koid, "cpu_total", stats.total_runtime);
    }
  }

  // |TaskEnumerator| Callback for a job.
  zx_status_t OnJob(int /*depth*/, zx_handle_t job, zx_koid_t koid,
                    zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::JOB);
    AddKoidValue(koid, "parent_koid", parent_koid);
    AddKoidName(job, koid);
    AddJobStats(job, koid);
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a process.
  zx_status_t OnProcess(int /*depth*/, zx_handle_t process, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::PROCESS);
    AddKoidValue(koid, "parent_koid", parent_koid);
    AddKoidName(process, koid);
    AddProcessStats(process, koid);
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a thread.
  zx_status_t OnThread(int /*depth*/, zx_handle_t thread, zx_koid_t koid,
                       zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::THREAD);
    AddKoidValue(koid, "parent_koid", parent_koid);
    AddKoidName(thread, koid);
    AddThreadStats(thread, koid);
    return ZX_OK;
  }

  // |TaskEnumerator| Enable On*() calls.
  bool has_on_job() const final { return true; }
  bool has_on_process() const final { return true; }
  bool has_on_thread() const final { return true; }
};

}  // namespace

void GatherTasks::Gather() {
  TaskHarvester task_harvester;
  task_harvester.WalkRootJobTree();
  task_harvester.UploadTaskInfo(DockyardPtr());
}

}  // namespace harvester
