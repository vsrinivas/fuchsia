#include "src/developer/memory/monitor/metrics.h"

#include <lib/syslog/cpp/logger.h>

#include <array>

#include <src/lib/cobalt/cpp/cobalt_event_builder.h>
#include <trace/event.h>

#include "src/developer/memory/metrics/digest.h"

namespace monitor {

using namespace memory;
using cobalt_registry::MemoryMetricDimensionBucket;

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
      }) {
  task_.PostDelayed(dispatcher_, zx::usec(1));
}

void Metrics::CollectMetrics() {
  TRACE_DURATION("memory_monitor", "Watcher::Metrics::CollectMetrics");
  Capture capture;
  capture_cb_(capture, KMEM);
  Digest digest(capture);

  std::vector<fuchsia::cobalt::CobaltEvent> events;

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
    events.push_back(builder.Clone()
                         .with_event_code_at(cobalt_registry::kMemoryMetricId, code_iter->second)
                         .as_memory_usage(bucket.size()));
  }
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  logger_->LogCobaltEvents(std::move(events), &status);
  if (status == fuchsia::cobalt::Status::INVALID_ARGUMENTS) {
    FX_LOGS(ERROR) << "CollectMetrics() returned status INVALID_ARGUMENTS";
  }
  task_.PostDelayed(dispatcher_, poll_frequency_);
}

}  // namespace monitor
