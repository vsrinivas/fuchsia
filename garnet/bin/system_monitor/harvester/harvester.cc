// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>
#include <task-utils/walker.h>
#include <zircon/status.h>

#include <chrono>
#include <memory>
#include <string>

#include "src/lib/fxl/logging.h"

namespace harvester {

namespace {

// Utility function to label and append a cpu sample to the |list|. |cpu| is the
// index returned from the kernel. |path| is the kind of sample, e.g.
// "interrupt_count".
void AddCpuValue(SampleList* list, size_t cpu, const std::string path,
                 dockyard::SampleValue value) {
  std::ostringstream label;
  label << "cpu:" << cpu << ":" << path;
  list->emplace_back(label.str(), value);
}

class TaskHarvester final : public TaskEnumerator {
 public:
  TaskHarvester() {}

  // After gathering the data, upload it to |dockyard|.
  void UploadTaskInfo(const std::unique_ptr<DockyardProxy>& dockyard_proxy) {
    // TODO(smbug.com/20): Send data to dockyard.
#ifdef VERBOSE_OUTPUT
    for (const auto& int_sample : int_sample_list_) {
      FXL_LOG(INFO) << int_sample.first << ": " << int_sample.second;
    }
    for (const auto& string_sample : string_sample_list_) {
      FXL_LOG(INFO) << string_sample.first << ": " << string_sample.second;
    }
#endif  // VERBOSE_OUTPUT
  }

 private:
  SampleList int_sample_list_;
  StringSampleList string_sample_list_;

  // Helper to add a value to the sample |int_sample_list_|.
  void AddKoidValue(zx_koid_t koid, const std::string path,
                    dockyard::SampleValue value) {
    std::ostringstream label;
    label << "koid:" << koid << ":" << path;
    int_sample_list_.emplace_back(label.str(), value);
  }

  // Helper to add a value to the string list.
  void AddKoidString(zx_koid_t koid, const std::string path,
                     std::string value) {
    std::ostringstream label;
    label << "koid:" << koid << ":" << path;
    string_sample_list_.emplace_back(label.str(), value);
  }

  // |TaskEnumerator| Callback for a job.
  zx_status_t OnJob(int depth, zx_handle_t job, zx_koid_t koid,
                    zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::JOB);
    AddKoidValue(koid, "parent_koid", parent_koid);
    // TODO(smbug.com/20): gather more info.
    return ZX_OK;
  }

  // |TaskEnumerator| Callback for a process.
  zx_status_t OnProcess(int depth, zx_handle_t process, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::PROCESS);
    AddKoidValue(koid, "parent_koid", parent_koid);

    char process_name[ZX_MAX_NAME_LEN];
    zx_status_t status = zx_object_get_property(
        process, ZX_PROP_NAME, &process_name, sizeof(process_name));
    AddKoidString(koid, "name", process_name);
#ifdef VERBOSE_OUTPUT
    FXL_LOG(INFO) << "process_name " << process_name;
#endif  // VERBOSE_OUTPUT
    // TODO(smbug.com/20): gather more info.

    return status;
  }

  // |TaskEnumerator| Callback for a thread.
  zx_status_t OnThread(int depth, zx_handle_t thread, zx_koid_t koid,
                       zx_koid_t parent_koid) override {
    AddKoidValue(koid, "type", dockyard::KoidType::THREAD);
    AddKoidValue(koid, "parent_koid", parent_koid);
    // TODO(smbug.com/20): gather more info.
    return ZX_OK;
  }

  // |TaskEnumerator| Enable On*() calls.
  bool has_on_job() const final { return true; }
  bool has_on_process() const final { return true; }
  bool has_on_thread() const final { return true; }
};

}  // namespace

std::ostream& operator<<(std::ostream& out, const DockyardProxyStatus& status) {
  switch (status) {
    case DockyardProxyStatus::OK:
      return out << "OK (0)";
    case DockyardProxyStatus::ERROR:
      return out << "ERROR (-1)";
  }
  FXL_NOTREACHED();
  return out;
}

Harvester::Harvester(zx::duration cycle_period, zx_handle_t root_resource,
                     async_dispatcher_t* dispatcher,
                     std::unique_ptr<DockyardProxy> dockyard_proxy)
    : cycle_period_(cycle_period),
      root_resource_(root_resource),
      dispatcher_(dispatcher),
      dockyard_proxy_(std::move(dockyard_proxy)) {}

void Harvester::GatherData() {
  GatherCpuSamples();
  GatherMemorySamples();
  GatherThreadSamples();
  GatherComponentIntrospection();
  // TODO(smbug.com/18): make this actually run at rate (i.e. remove drift from
  // execution time).
  async::PostDelayedTask(
      dispatcher_, [this] { GatherData(); }, cycle_period_);
}

void Harvester::GatherCpuSamples() {
  // TODO(smbug.com/34): Determine the array size at runtime (32 is arbitrary).
  zx_info_cpu_stats_t stats[32];
  size_t actual, avail;
  zx_status_t err = zx_object_get_info(root_resource_, ZX_INFO_CPU_STATS,
                                       &stats, sizeof(stats), &actual, &avail);
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
    AddCpuValue(&list, i, "meaningful_irq_preempts", stats[i].irq_preempts);
    AddCpuValue(&list, i, "preempts", stats[i].preempts);
    AddCpuValue(&list, i, "yields", stats[i].yields);

    // CPU level interrupts and exceptions.
    uint64_t busy_time =
        cpu_time > stats[i].idle_time ? cpu_time - stats[i].idle_time : 0ull;
    AddCpuValue(&list, i, "busy_time", busy_time);
    AddCpuValue(&list, i, "idle_time", stats[i].idle_time);
    AddCpuValue(&list, i, "external_hardware_interrupts", stats[i].ints);
    AddCpuValue(&list, i, "timer_interrupts", stats[i].timer_ints);
    AddCpuValue(&list, i, "timer_callbacks", stats[i].timers);
    AddCpuValue(&list, i, "syscalls", stats[i].syscalls);

    // Inter-processor interrupts.
    AddCpuValue(&list, i, "reschedule_ipis", stats[i].reschedule_ipis);
    AddCpuValue(&list, i, "generic_ipis", stats[i].generic_ipis);
  }
  DockyardProxyStatus status = dockyard_proxy_->SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

void Harvester::GatherMemorySamples() {
  zx_info_kmem_stats_t stats;
  zx_status_t err = zx_object_get_info(root_resource_, ZX_INFO_KMEM_STATS,
                                       &stats, sizeof(stats), NULL, NULL);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS error " << zx_status_get_string(err);
    return;
  }

#ifdef VERBOSE_OUTPUT
  FXL_LOG(INFO) << "free memory total " << stats.free_bytes << ", heap "
                << stats.free_heap_bytes << ", vmo " << stats.vmo_bytes
                << ", mmu " << stats.mmu_overhead_bytes << ", ipc "
                << stats.ipc_bytes;
#endif  // VERBOSE_OUTPUT

  const std::string DEVICE_TOTAL = "memory:device_total_bytes";
  const std::string DEVICE_FREE = "memory:device_free_bytes";

  const std::string KERNEL_TOTAL = "memory:kernel_total_bytes";
  const std::string KERNEL_FREE = "memory:kernel_free_bytes";
  const std::string KERNEL_OTHER = "memory:kernel_other_bytes";

  const std::string VMO = "memory:vmo_bytes";
  const std::string MMU_OVERHEAD = "memory:mmu_overhead_bytes";
  const std::string IPC = "memory:ipc_bytes";
  const std::string OTHER = "memory:device_other_bytes";

  SampleList list;
  // Memory for the entire machine.
  list.push_back(std::make_pair(DEVICE_TOTAL, stats.total_bytes));
  list.push_back(std::make_pair(DEVICE_FREE, stats.free_bytes));
  // Memory in the kernel.
  list.push_back(std::make_pair(KERNEL_TOTAL, stats.total_heap_bytes));
  list.push_back(std::make_pair(KERNEL_FREE, stats.free_heap_bytes));
  list.push_back(std::make_pair(KERNEL_OTHER, stats.wired_bytes));
  // Categorized memory.
  list.push_back(std::make_pair(MMU_OVERHEAD, stats.mmu_overhead_bytes));
  list.push_back(std::make_pair(VMO, stats.vmo_bytes));
  list.push_back(std::make_pair(IPC, stats.ipc_bytes));
  list.push_back(std::make_pair(OTHER, stats.other_bytes));

  DockyardProxyStatus status = dockyard_proxy_->SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

void Harvester::GatherThreadSamples() {
  TaskHarvester task_harvester;
  task_harvester.WalkRootJobTree();
  task_harvester.UploadTaskInfo(dockyard_proxy_);
}

void Harvester::GatherComponentIntrospection() {
  std::string fake_json_data = "{ \"test\": 5 }";
  DockyardProxyStatus status = dockyard_proxy_->SendInspectJson(
      "inspect:/hub/fake/234/faux.Inspect", fake_json_data);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

}  // namespace harvester
