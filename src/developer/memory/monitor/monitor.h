// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_MONITOR_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_MONITOR_H_

#include <fuchsia/hardware/ram/metrics/cpp/fidl.h>
#include <fuchsia/memory/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/observer.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>

#include "lib/sys/inspect/cpp/component.h"
#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/monitor/high_water.h"
#include "src/developer/memory/monitor/metrics.h"
#include "src/developer/memory/monitor/pressure_notifier.h"
#include "src/lib/fxl/command_line.h"

namespace monitor {

namespace test {
class MonitorUnitTest;
class MemoryBandwidthInspectTest;
}  // namespace test

class Monitor : public fuchsia::memory::Monitor {
 public:
  Monitor(std::unique_ptr<sys::ComponentContext> context, const fxl::CommandLine& command_line,
          async_dispatcher_t* dispatcher, bool send_metrics, bool watch_memory_pressure,
          bool send_critical_pressure_crash_reports);
  ~Monitor();

  // For memory bandwidth measurement, SetRamDevice should be called once
  void SetRamDevice(fuchsia::hardware::ram::metrics::DevicePtr ptr);

  void Watch(fidl::InterfaceHandle<fuchsia::memory::Watcher> watcher) override;
  static const char kTraceName[];

 private:
  void CreateMetrics();

  void UpdateState();

  void StartTracing();
  void StopTracing();

  void SampleAndPost();
  void MeasureBandwidthAndPost();
  void PeriodicMeasureBandwidth();
  void PrintHelp();
  inspect::Inspector Inspect();

  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(fuchsia::memory::Watcher* watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const zx_info_kmem_stats_extended_t& stats);

  memory::CaptureState capture_state_;
  HighWater high_water_;
  uint64_t prealloc_size_;
  zx::vmo prealloc_vmo_;
  bool logging_;
  bool tracing_;
  zx::duration delay_;
  zx_handle_t root_;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  fuchsia::cobalt::LoggerSyncPtr logger_;
  fidl::BindingSet<fuchsia::memory::Monitor> bindings_;
  std::vector<fuchsia::memory::WatcherPtr> watchers_;
  trace::TraceObserver trace_observer_;
  sys::ComponentInspector inspector_;
  std::unique_ptr<Metrics> metrics_;
  std::unique_ptr<PressureNotifier> pressure_notifier_;
  fuchsia::hardware::ram::metrics::DevicePtr ram_device_;
  uint64_t pending_bandwidth_measurements_ = 0;

  friend class test::MonitorUnitTest;
  friend class test::MemoryBandwidthInspectTest;
  FXL_DISALLOW_COPY_AND_ASSIGN(Monitor);
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_MONITOR_H_
