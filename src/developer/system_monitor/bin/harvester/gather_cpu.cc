// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_cpu.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "harvester.h"
#include "sample_bundle.h"

namespace harvester {

namespace {

// Utility function to label and append a cpu sample to the |list|. |cpu| is the
// index returned from the kernel. |path| is the kind of sample, e.g.
// "interrupt_count".
void AddCpuValue(SampleBundle* samples, size_t cpu, const std::string& path,
                 dockyard::SampleValue value) {
  samples->AddIntSample("cpu", cpu, path, value);
}

}  // namespace

void AddGlobalCpuSamples(SampleBundle* samples, zx_handle_t root_resource) {
  // TODO(fxbug.dev/34): Determine the array size at runtime (32 is arbitrary).
  zx_info_cpu_stats_t stats[32];
  size_t actual, avail;
  zx_status_t err = zx_object_get_info(root_resource, ZX_INFO_CPU_STATS, &stats,
                                       sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    FX_LOGS(ERROR) << ZxErrorString("ZX_INFO_CPU_STATS", err);
    return;
  }
  auto now = std::chrono::high_resolution_clock::now();
  auto cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      now.time_since_epoch())
                      .count();
  for (size_t i = 0; i < actual; ++i) {
    // Note: stats[i].flags are not currently recorded.

    // Kernel scheduler counters.
    AddCpuValue(samples, i, "reschedules", stats[i].reschedules);
    AddCpuValue(samples, i, "context_switches", stats[i].context_switches);
    AddCpuValue(samples, i, "meaningful_irq_preempts", stats[i].irq_preempts);
    AddCpuValue(samples, i, "preempts", stats[i].preempts);
    AddCpuValue(samples, i, "yields", stats[i].yields);

    // CPU level interrupts and exceptions.
    uint64_t busy_time =
        cpu_time > stats[i].idle_time ? cpu_time - stats[i].idle_time : 0ull;
    AddCpuValue(samples, i, "busy_time", busy_time);
    AddCpuValue(samples, i, "idle_time", stats[i].idle_time);
    AddCpuValue(samples, i, "external_hardware_interrupts", stats[i].ints);
    AddCpuValue(samples, i, "timer_interrupts", stats[i].timer_ints);
    AddCpuValue(samples, i, "timer_callbacks", stats[i].timers);
    AddCpuValue(samples, i, "syscalls", stats[i].syscalls);

    // Inter-processor interrupts.
    AddCpuValue(samples, i, "reschedule_ipis", stats[i].reschedule_ipis);
    AddCpuValue(samples, i, "generic_ipis", stats[i].generic_ipis);
  }
}

void GatherCpu::GatherDeviceProperties() {
  const std::string CPU_COUNT = "cpu:count";
  zx_info_cpu_stats_t stats[1];
  size_t actual, avail;
  zx_status_t err = zx_object_get_info(RootResource(), ZX_INFO_CPU_STATS,
                                       &stats, sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    FX_LOGS(ERROR) << ZxErrorString("ZX_INFO_CPU_STATS", err);
    return;
  }
  SampleList list;
  list.emplace_back(CPU_COUNT, avail);
  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FX_LOGS(ERROR) << DockyardErrorString("SendSampleList", status)
                   << " The cpu_count value will be missing";
  }
}

void GatherCpu::Gather() {
  SampleBundle samples;
  AddGlobalCpuSamples(&samples, RootResource());
  samples.Upload(DockyardPtr());
}

}  // namespace harvester
