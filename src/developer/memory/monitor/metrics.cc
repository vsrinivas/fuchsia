// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/metrics.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/time.h>

#include <algorithm>
#include <array>
#include <string>

#include <src/lib/cobalt/cpp/cobalt_event_builder.h>

#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/printer.h"

namespace monitor {

using namespace memory;
using cobalt_registry::MemoryMetricDimensionBucket;
using TimeSinceBoot = cobalt_registry::MemoryLeakMetricDimensionTimeSinceBoot;

namespace {
static const std::map<zx_duration_t, TimeSinceBoot> UptimeLevelMap = {
    {zx_duration_from_min(1), TimeSinceBoot::Up},
    {zx_duration_from_min(30), TimeSinceBoot::UpOneMinute},
    {zx_duration_from_hour(1), TimeSinceBoot::UpThirtyMinutes},
    {zx_duration_from_hour(6), TimeSinceBoot::UpOneHour},
    {zx_duration_from_hour(12), TimeSinceBoot::UpSixHours},
    {zx_duration_from_hour(24), TimeSinceBoot::UpTwelveHours},
    {zx_duration_from_hour(48), TimeSinceBoot::UpOneDay},
    {zx_duration_from_hour(72), TimeSinceBoot::UpTwoDays},
    {zx_duration_from_hour(144), TimeSinceBoot::UpThreeDays},
};
}  // namespace

// Metrics polls the memory state periodically asynchroniously using the passed dispatcher and sends
// information about the memory Digests to Cobalt, in the form of several Events.
Metrics::Metrics(zx::duration poll_frequency, async_dispatcher_t* dispatcher,
                 sys::ComponentInspector* inspector, fuchsia::cobalt::Logger_Sync* logger,
                 CaptureFn capture_cb)
    : poll_frequency_(poll_frequency),
      dispatcher_(dispatcher),
      logger_(logger),
      capture_cb_(std::move(capture_cb)),
      bucket_name_to_code_({
          {"TotalBytes", MemoryMetricDimensionBucket::TotalBytes},
          {"ZBI Buffer", MemoryMetricDimensionBucket::ZbiBuffer},
          {"Graphics", MemoryMetricDimensionBucket::Graphics},
          {"Video Buffer", MemoryMetricDimensionBucket::VideoBuffer},
          {"Minfs", MemoryMetricDimensionBucket::Minfs},
          {"Blobfs", MemoryMetricDimensionBucket::Blobfs},
          {"BlobfsInactive", MemoryMetricDimensionBucket::BlobfsInactive},
          {"Opal", MemoryMetricDimensionBucket::Opal},
          {"Web", MemoryMetricDimensionBucket::Web},
          {"Kronk", MemoryMetricDimensionBucket::Kronk},
          {"Scenic", MemoryMetricDimensionBucket::Scenic},
          {"Amlogic", MemoryMetricDimensionBucket::Amlogic},
          {"Netstack", MemoryMetricDimensionBucket::Netstack},
          {"Amber", MemoryMetricDimensionBucket::Amber},
          {"Pkgfs", MemoryMetricDimensionBucket::Pkgfs},
          {"Cast", MemoryMetricDimensionBucket::Cast},
          {"Chromium", MemoryMetricDimensionBucket::Chromium},
          {"Free", MemoryMetricDimensionBucket::Free},
          {"Kernel", MemoryMetricDimensionBucket::Kernel},
          {"Orphaned", MemoryMetricDimensionBucket::Orphaned},
          {"Undigested", MemoryMetricDimensionBucket::Undigested},
          {"Fshost", MemoryMetricDimensionBucket::Fshost},
          {"Flutter", MemoryMetricDimensionBucket::Flutter},
          {"Archivist", MemoryMetricDimensionBucket::Archivist},
          {"Cobalt", MemoryMetricDimensionBucket::Cobalt},
          {"Audio", MemoryMetricDimensionBucket::Audio},
          {"Context", MemoryMetricDimensionBucket::Context},
          {"ContiguousPool", MemoryMetricDimensionBucket::ContiguousPool},
          {"ProtectedPool", MemoryMetricDimensionBucket::ProtectedPool},
          {"FlutterApps", MemoryMetricDimensionBucket::FlutterApps},
      }),
      inspector_(inspector),
      platform_metric_node_(inspector_->root().CreateChild(kInspectPlatformNodeName)),
      // Diagram of hierarchy can be seen below:
      // root
      // - platform_metrics
      //   - memory_usages
      //     - Amber
      //     - Amlogic
      //     - ...
      //     - timestamp
      //   - memory_bandwidth
      //     - readings
      metric_memory_node_(platform_metric_node_.CreateChild(kMemoryNodeName)),
      inspect_memory_timestamp_(metric_memory_node_.CreateInt(kReadingMemoryTimestamp, 0)),
      metric_memory_bandwidth_node_(platform_metric_node_.CreateChild(kMemoryBandwidthNodeName)),
      inspect_memory_bandwidth_(metric_memory_bandwidth_node_.
          CreateUintArray(kReadings, kMemoryBandwidthArraySize)),
      inspect_memory_bandwidth_timestamp_(
          metric_memory_bandwidth_node_.CreateInt(kReadingMemoryTimestamp, 0)) {
  for (auto& element : bucket_name_to_code_) {
    inspect_memory_usages_.insert(std::pair<std::string, inspect::UintProperty>(
        element.first, metric_memory_node_.CreateUint(element.first, 0)));
  }

  task_.PostDelayed(dispatcher_, zx::usec(1));
}

void Metrics::CollectMetrics() {
  TRACE_DURATION("memory_monitor", "Watcher::Metrics::CollectMetrics");
  Capture capture;
  capture_cb_(&capture, VMO);
  Digest digest(capture, &digester_);
  std::ostringstream oss;
  Printer p(oss);

  p.PrintDigest(digest);
  auto str = oss.str();
  std::replace(str.begin(), str.end(), '\n', ' ');
  FX_LOGS(INFO) << str;

  WriteDigestToInspect(digest);

  std::vector<fuchsia::cobalt::CobaltEvent> events;
  const auto& kmem = capture.kmem();
  AddKmemEvents(kmem, &events);
  AddKmemEventsWithUptime(kmem, capture.time(), &events);
  auto builder = cobalt::CobaltEventBuilder(cobalt_registry::kMemoryMetricId);
  for (const auto& bucket : digest.buckets()) {
    if (bucket.size() == 0) {
      continue;
    }
    const auto& code_iter = bucket_name_to_code_.find(bucket.name());
    if (code_iter == bucket_name_to_code_.end()) {
      FX_LOGS_FIRST_N(ERROR, 3) << "Metrics::CollectMetrics: Invalid bucket name: "
                                << bucket.name();
      continue;
    }
    events.push_back(
        builder.Clone().with_event_code(code_iter->second).as_memory_usage(bucket.size()));
  }
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  logger_->LogCobaltEvents(std::move(events), &status);
  if (status == fuchsia::cobalt::Status::INVALID_ARGUMENTS) {
    FX_LOGS(ERROR) << "LogCobaltEvents() returned status INVALID_ARGUMENTS";
  }
  task_.PostDelayed(dispatcher_, poll_frequency_);
}

void Metrics::WriteDigestToInspect(const memory::Digest& digest) {
  TRACE_DURATION("memory_monitor", "Watcher::Metrics::WriteDigestToInspect");
  // The division is to address JSON int range problem in b/156523968
  // JSON does not suport 64 bit integers and some readers using JSON libraries will
  // crash if we write a value greater than MAX_INT_32.
  // The unit in seconds is sufficient.
  inspect_memory_timestamp_.Set(digest.time() / 1000000000);
  for (auto const& bucket : digest.buckets()) {
    if (inspect_memory_usages_.find(bucket.name()) != inspect_memory_usages_.end()) {
      inspect_memory_usages_[bucket.name()].Set(bucket.size());
    } else {
      FX_LOGS(INFO) << "Not_in_map: " << bucket.name() << ": " << bucket.size() << "\n";
    }
  }
}

void Metrics::AddKmemEvents(const zx_info_kmem_stats_t& kmem,
                            std::vector<fuchsia::cobalt::CobaltEvent>* events) {
  TRACE_DURATION("memory_monitor", "Metrics::AddKmemEvents");
  auto builder = cobalt::CobaltEventBuilder(cobalt_registry::kMemoryGeneralBreakdownMetricId);
  using Breakdown = cobalt_registry::MemoryGeneralBreakdownMetricDimensionGeneralBreakdown;
  events->push_back(
      builder.Clone().with_event_code(Breakdown::TotalBytes).as_memory_usage(kmem.total_bytes));
  events->push_back(builder.Clone()
                        .with_event_code(Breakdown::UsedBytes)
                        .as_memory_usage(kmem.total_bytes - kmem.free_bytes));
  events->push_back(
      builder.Clone().with_event_code(Breakdown::FreeBytes).as_memory_usage(kmem.free_bytes));
  events->push_back(
      builder.Clone().with_event_code(Breakdown::VmoBytes).as_memory_usage(kmem.vmo_bytes));
  events->push_back(builder.Clone()
                        .with_event_code(Breakdown::KernelFreeHeapBytes)
                        .as_memory_usage(kmem.free_heap_bytes));
  events->push_back(builder.Clone()
                        .with_event_code(Breakdown::MmuBytes)
                        .as_memory_usage(kmem.mmu_overhead_bytes));
  events->push_back(
      builder.Clone().with_event_code(Breakdown::IpcBytes).as_memory_usage(kmem.ipc_bytes));
  events->push_back(builder.Clone()
                        .with_event_code(Breakdown::KernelTotalHeapBytes)
                        .as_memory_usage(kmem.total_heap_bytes));
  events->push_back(
      builder.Clone().with_event_code(Breakdown::WiredBytes).as_memory_usage(kmem.wired_bytes));
  events->push_back(
      builder.Clone().with_event_code(Breakdown::OtherBytes).as_memory_usage(kmem.other_bytes));
}

// TODO(fxbug.dev/3778): Refactor this when dedup enum is availble in generated
// cobalt config source code.
void Metrics::AddKmemEventsWithUptime(const zx_info_kmem_stats_t& kmem,
                                      const zx_time_t capture_time,
                                      std::vector<fuchsia::cobalt::CobaltEvent>* events) {
  TRACE_DURATION("memory_monitor", "Metrics::AddKmemEventsWithUptime");
  auto builder = std::move(cobalt::CobaltEventBuilder(cobalt_registry::kMemoryLeakMetricId)
                               .with_event_code_at(1, GetUpTimeEventCode(capture_time)));
  using Breakdown = cobalt_registry::MemoryLeakMetricDimensionGeneralBreakdown;
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::TotalBytes)
                        .as_memory_usage(kmem.total_bytes));
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::UsedBytes)
                        .as_memory_usage(kmem.total_bytes - kmem.free_bytes));
  events->push_back(
      builder.Clone().with_event_code_at(0, Breakdown::FreeBytes).as_memory_usage(kmem.free_bytes));
  events->push_back(
      builder.Clone().with_event_code_at(0, Breakdown::VmoBytes).as_memory_usage(kmem.vmo_bytes));
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::KernelFreeHeapBytes)
                        .as_memory_usage(kmem.free_heap_bytes));
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::MmuBytes)
                        .as_memory_usage(kmem.mmu_overhead_bytes));
  events->push_back(
      builder.Clone().with_event_code_at(0, Breakdown::IpcBytes).as_memory_usage(kmem.ipc_bytes));
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::KernelTotalHeapBytes)
                        .as_memory_usage(kmem.total_heap_bytes));
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::WiredBytes)
                        .as_memory_usage(kmem.wired_bytes));
  events->push_back(builder.Clone()
                        .with_event_code_at(0, Breakdown::OtherBytes)
                        .as_memory_usage(kmem.other_bytes));
}

TimeSinceBoot Metrics::GetUpTimeEventCode(const zx_time_t capture_time) {
  zx_duration_t uptime = zx_duration_from_nsec(capture_time);
  for (auto const& map : UptimeLevelMap) {
    if (uptime < map.first) {
      return map.second;
    }
  }
  return TimeSinceBoot::UpSixDays;
}

void Metrics::NextMemoryBandwidthReading(uint64_t reading, zx_time_t ts) {
  inspect_memory_bandwidth_.Set(memory_bandwidth_index_, reading);
  inspect_memory_bandwidth_timestamp_.Set(ts);
  if (++memory_bandwidth_index_ >= kMemoryBandwidthArraySize)
    memory_bandwidth_index_ = 0;
}

}  // namespace monitor
