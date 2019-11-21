#include "src/developer/memory/monitor/metrics.h"

#include <zircon/time.h>

#include <array>

#include <src/lib/cobalt/cpp/cobalt_event_builder.h>
#include <trace/event.h>

#include "src/developer/memory/metrics/digest.h"
#include "src/lib/syslog/cpp/logger.h"

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
                 fuchsia::cobalt::Logger_Sync* logger, CaptureFn capture_cb)
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
      }) {
  task_.PostDelayed(dispatcher_, zx::usec(1));
}

void Metrics::CollectMetrics() {
  TRACE_DURATION("memory_monitor", "Watcher::Metrics::CollectMetrics");
  Capture capture;
  capture_cb_(&capture, VMO);
  Digest digest(capture, &digester_);
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

// TODO(fxb/3778) Refactor this when dedup enum is availble in generated
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

}  // namespace monitor
