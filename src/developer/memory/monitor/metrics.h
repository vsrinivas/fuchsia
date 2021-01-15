// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_METRICS_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_METRICS_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/inspect/cpp/component.h>

#include <string>
#include <unordered_map>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/watcher.h"
#include "src/developer/memory/monitor/memory_metrics_registry.cb.h"
#include "src/lib/fxl/macros.h"

namespace monitor {

class Metrics {
 public:
  Metrics(zx::duration poll_frequency, async_dispatcher_t* dispatcher,
          sys::ComponentInspector* inspector, fuchsia::cobalt::Logger_Sync* logger,
          memory::CaptureFn capture_cb);

  // Allow monitor to update the memory bandwidth readings
  // once a second to metrics
  void NextMemoryBandwidthReading(uint64_t reading, zx_time_t ts);

  // Reader side must use the exact name to read from Inspect.
  // Design doc in go/fuchsia-metrics-to-inspect-design.
  static constexpr const char* kInspectPlatformNodeName = "platform_metrics";
  // Details about config file are in b/151984065#comment16
  static constexpr const char* kMemoryNodeName = "memory_usages";
  static constexpr const char* kReadingMemoryTimestamp = "timestamp";
  static constexpr const char* kMemoryBandwidthNodeName = "memory_bandwidth";
  static constexpr const char* kReadings = "readings";

  // Size of the circular buffer for readings within a minute
  static constexpr size_t kMemoryBandwidthArraySize = 60;

 private:
  void CollectMetrics();
  void WriteDigestToInspect(const memory::Digest& digest);
  void AddKmemEvents(const zx_info_kmem_stats_t& kmem,
                     std::vector<fuchsia::cobalt::CobaltEvent>* events);
  void AddKmemEventsWithUptime(const zx_info_kmem_stats_t& kmem, zx_time_t capture_time,
                               std::vector<fuchsia::cobalt::CobaltEvent>* events);
  cobalt_registry::MemoryLeakMetricDimensionTimeSinceBoot GetUpTimeEventCode(
      const zx_time_t current);

  zx::duration poll_frequency_;
  async_dispatcher_t* dispatcher_;
  fuchsia::cobalt::Logger_Sync* logger_;
  memory::CaptureFn capture_cb_;
  async::TaskClosureMethod<Metrics, &Metrics::CollectMetrics> task_{this};
  std::unordered_map<std::string, cobalt_registry::MemoryMetricDimensionBucket>
      bucket_name_to_code_;
  memory::Digester digester_;

  // The component inspector to publish data to.
  // Not owned.
  sys::ComponentInspector* inspector_;
  inspect::Node platform_metric_node_;
  inspect::Node metric_memory_node_;
  std::map<std::string, inspect::UintProperty> inspect_memory_usages_;
  inspect::IntProperty inspect_memory_timestamp_;

  inspect::Node metric_memory_bandwidth_node_;
  inspect::UintArray inspect_memory_bandwidth_;
  inspect::IntProperty inspect_memory_bandwidth_timestamp_;

  size_t memory_bandwidth_index_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Metrics);
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_METRICS_H_
