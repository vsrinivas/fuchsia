// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <chrono>
#include <memory>
#include <string>

#include <fuchsia/memory/cpp/fidl.h>
#include <task-utils/walker.h>
#include <zircon/status.h>

#include "lib/fxl/logging.h"

namespace harvester {

namespace {

// Utility function to label and append a cpu sample to the |list|. |cpu| is the
// index returned from the kernel. |name| is the kind of sample, e.g.
// "interrupt_count".
void AddCpuValue(SampleList* list, size_t cpu, const std::string name,
                 dockyard::SampleValue value) {
  std::ostringstream label;
  label << "cpu:" << cpu << ":" << name;
  list->emplace_back(label.str(), value);
}

class TaskHarvester final : public TaskEnumerator {
 public:
  TaskHarvester() {}

  // After gathering the data, upload it to |harvester|.
  void UploadTaskInfo(const std::unique_ptr<Harvester>& harvester) {
    // TODO(dschuyler): Send data to dockyard.
    for (auto iter = list_.begin(); iter != list_.end(); ++iter) {
      FXL_LOG(INFO) << iter->first << ": " << iter->second;
    }
  }

 private:
  SampleList list_;

  // Helper to add a value to the sample |list|.
  void AddKoidValue(zx_koid_t koid, const std::string name,
                    dockyard::SampleValue value) {
    std::ostringstream label;
    label << "koid:" << koid << ":" << name;
    list_.emplace_back(label.str(), value);
  }

  // |TaskEnumerator| Callback for a job.
  zx_status_t OnJob(int depth, zx_handle_t job, zx_koid_t koid,
                    zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::JOB);
    AddKoidValue(koid, "parent_koid", parent_koid);
    // TODO(dschuyler): gather more info.
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a process.
  zx_status_t OnProcess(int depth, zx_handle_t process, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::PROCESS);
    AddKoidValue(koid, "parent_koid", parent_koid);
    // TODO(dschuyler): gather more info.
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a thread.
  zx_status_t OnThread(int depth, zx_handle_t thread, zx_koid_t koid,
                       zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::THREAD);
    AddKoidValue(koid, "parent_koid", parent_koid);
    // TODO(dschuyler): gather more info.
    return ZX_OK;
  }

  // |TaskEnumerator| Enable On*() calls.
  bool has_on_job() const final { return true; }
  bool has_on_process() const final { return true; }
  bool has_on_thread() const final { return true; }
};

}  // namespace

std::ostream& operator<<(std::ostream& out, const HarvesterStatus& status) {
  switch (status) {
    case HarvesterStatus::OK:
      return out << "OK (0)";
    case HarvesterStatus::ERROR:
      return out << "ERROR (-1)";
  }
  FXL_NOTREACHED();
  return out;
}

void GatherCpuSamples(zx_handle_t root_resource,
                      const std::unique_ptr<harvester::Harvester>& harvester) {
  // TODO(dschuyler): Determine the array size at runtime (32 is arbitrary).
  zx_info_cpu_stats_t stats[32];
  size_t actual, avail;
  zx_status_t err = zx_object_get_info(root_resource, ZX_INFO_CPU_STATS, &stats,
                                       sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "ZX_INFO_CPU_STATS returned " << err << "("
                   << zx_status_get_string(err) << ")";
    return;
  }
  auto now = std::chrono::high_resolution_clock::now();
  auto cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      now.time_since_epoch())
                      .count();
  SampleList list;
  for (size_t i = 0; i < actual; ++i) {
    // Note: stats[i].flags are not currently recorded.

    // Kernel scheduler counters.
    AddCpuValue(&list, i, "reschedules", stats[i].reschedules);
    AddCpuValue(&list, i, "context_switches", stats[i].context_switches);
    AddCpuValue(&list, i, "irq_preempts", stats[i].irq_preempts);
    AddCpuValue(&list, i, "preempts", stats[i].preempts);
    AddCpuValue(&list, i, "yields", stats[i].yields);

    // CPU level interrupts and exceptions.
    uint64_t busy_time =
        cpu_time > stats[i].idle_time ? cpu_time - stats[i].idle_time : 0ull;
    AddCpuValue(&list, i, "busy_time", busy_time);
    AddCpuValue(&list, i, "idle_time", stats[i].idle_time);
    AddCpuValue(&list, i, "hardware_interrupts", stats[i].ints);
    AddCpuValue(&list, i, "timer_interrupts", stats[i].timer_ints);
    AddCpuValue(&list, i, "timer_callbacks", stats[i].timers);
    AddCpuValue(&list, i, "syscalls", stats[i].syscalls);

    // Inter-processor interrupts.
    AddCpuValue(&list, i, "reschedule_ipis", stats[i].reschedule_ipis);
    AddCpuValue(&list, i, "generic_ipis", stats[i].generic_ipis);
  }
  HarvesterStatus status = harvester->SendSampleList(list);
  if (status != HarvesterStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

void GatherMemorySamples(
    zx_handle_t root_resource,
    const std::unique_ptr<harvester::Harvester>& harvester) {
  zx_info_kmem_stats_t stats;
  zx_status_t err = zx_object_get_info(root_resource, ZX_INFO_KMEM_STATS,
                                       &stats, sizeof(stats), NULL, NULL);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS error " << zx_status_get_string(err);
    return;
  }

  FXL_LOG(INFO) << "free memory total " << stats.free_bytes << ", heap "
                << stats.free_heap_bytes << ", vmo " << stats.vmo_bytes
                << ", mmu " << stats.mmu_overhead_bytes << ", ipc "
                << stats.ipc_bytes;

  const std::string FREE_BYTES = "memory:free_bytes";
  const std::string FREE_HEAP_BYTES = "memory:free_heap_bytes";
  const std::string VMO_BYTES = "memory:vmo_bytes";
  const std::string MMU_OVERHEAD_BYTES = "memory:mmu_overhead_by";
  const std::string IPC_BYTES = "memory:ipc_bytes";

  SampleList list;
  list.push_back(std::make_pair(FREE_BYTES, stats.free_bytes));
  list.push_back(std::make_pair(MMU_OVERHEAD_BYTES, stats.mmu_overhead_bytes));
  list.push_back(std::make_pair(FREE_HEAP_BYTES, stats.free_heap_bytes));
  list.push_back(std::make_pair(VMO_BYTES, stats.vmo_bytes));
  list.push_back(std::make_pair(IPC_BYTES, stats.ipc_bytes));
  HarvesterStatus status = harvester->SendSampleList(list);
  if (status != HarvesterStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

void GatherThreadSamples(
    zx_handle_t root_resource,
    const std::unique_ptr<harvester::Harvester>& harvester) {
  TaskHarvester task_harvester;
  task_harvester.UploadTaskInfo(harvester);
}

}  // namespace harvester
