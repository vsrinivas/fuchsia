// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_threads_and_cpu.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "gather_cpu.h"
#include "gather_tasks.h"
#include "sample_bundle.h"
#include "task_tree.h"

namespace harvester {

namespace {

// Utilities to create a SampleBundle with channel information.
class SampleBundleBuilder final {
 public:
  explicit SampleBundleBuilder(SampleBundle* samples)
      : sample_bundle_(samples) {}

  // Helper to add a value to the sample |int_sample_list_|.
  void AddKoidValue(zx_koid_t koid, const std::string& path,
                    dockyard::SampleValue value);

  void AddCpuValue(size_t cpu, const std::string& path,
                   dockyard::SampleValue value);

 private:
  SampleBundle* sample_bundle_;

  SampleBundleBuilder() = delete;
};

// Add a value to the samples.
void SampleBundleBuilder::AddKoidValue(zx_koid_t koid, const std::string& path,
                                       dockyard::SampleValue value) {
  sample_bundle_->AddIntSample("koid", koid, path, value);
}

void SampleBundleBuilder::AddCpuValue(size_t cpu, const std::string& path,
                                      dockyard::SampleValue value) {
  sample_bundle_->AddIntSample("cpu", cpu, path, value);
}

}  // namespace

void AddThreadStats(SampleBundle* samples,
                    const std::vector<TaskTree::Task>& threads,
                    OS* os) {
  SampleBundleBuilder builder(samples);
  zx_status_t status;
  zx_info_thread_t info;
  zx_info_thread_stats_t stats;

  for (const TaskTree::Task& thread : threads) {
    status = os->GetInfo<zx_info_thread_t>(thread.handle, thread.koid,
                                           ZX_INFO_THREAD, "ZX_INFO_THREAD",
                                           info);
    if (status != ZX_OK) {
      continue;
    }

    status = os->GetInfo<zx_info_thread_stats_t>(thread.handle, thread.koid,
                                                 ZX_INFO_THREAD_STATS,
                                                 "ZX_INFO_THREAD_STATS",
                                                 stats);
    if (status != ZX_OK) {
      continue;
    }

    builder.AddKoidValue(thread.koid, "thread_state", info.state);
    builder.AddKoidValue(thread.koid, "cpu_total", stats.total_runtime);
  }
}

void AddGlobalCpuSamples(SampleBundle* samples, zx_handle_t info_resource,
                         OS* os) {
  SampleBundleBuilder builder(samples);

  std::vector<zx_info_cpu_stats_t> stats;
  zx_status_t status =
      os->GetChildren<zx_info_cpu_stats_t>(info_resource,
                                           0 /* no koid for info resource */,
                                           ZX_INFO_CPU_STATS,
                                           "ZX_INFO_CPU_STATS", stats);
  if (status != ZX_OK) {
    return;
  }

  auto cpu_time = os->HighResolutionNow();
  for (size_t i = 0; i < stats.size(); ++i) {
    const zx_info_cpu_stats_t& stat = stats[i];
    // Note: stat.flags are not currently recorded.

    // Kernel scheduler counters.
    builder.AddCpuValue(i, "reschedules", stat.reschedules);
    builder.AddCpuValue(i, "context_switches", stat.context_switches);
    builder.AddCpuValue(i, "meaningful_irq_preempts", stat.irq_preempts);
    builder.AddCpuValue(i, "preempts", stat.preempts);
    builder.AddCpuValue(i, "yields", stat.yields);

    // CPU level interrupts and exceptions.
    uint64_t busy_time =
        cpu_time > stat.idle_time ? cpu_time - stat.idle_time : 0ull;
    builder.AddCpuValue(i, "busy_time", busy_time);
    builder.AddCpuValue(i, "idle_time", stat.idle_time);
    builder.AddCpuValue(i, "external_hardware_interrupts", stat.ints);
    builder.AddCpuValue(i, "timer_interrupts", stat.timer_ints);
    builder.AddCpuValue(i, "timer_callbacks", stat.timers);
    builder.AddCpuValue(i, "syscalls", stat.syscalls);

    // Inter-processor interrupts.
    builder.AddCpuValue(i, "reschedule_ipis", stat.reschedule_ipis);
    builder.AddCpuValue(i, "generic_ipis", stat.generic_ipis);
  }
}

void GatherThreadsAndCpu::Gather() {
  SampleBundle samples;

  limiter_.Run([&] {
    task_tree_.Gather();
    AddTaskBasics(&samples, task_tree_.Jobs(), dockyard::KoidType::JOB);
    AddTaskBasics(&samples, task_tree_.Processes(),
                  dockyard::KoidType::PROCESS);
    AddTaskBasics(&samples, task_tree_.Threads(), dockyard::KoidType::THREAD);
  });

  AddThreadStats(&samples, task_tree_.Threads(), os_);
  AddGlobalCpuSamples(&samples, InfoResource(), os_);
  samples.Upload(DockyardPtr());
}

}  // namespace harvester
