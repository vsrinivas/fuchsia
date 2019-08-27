#ifndef SRC_DEVELOPER_MEMORY_MONITOR_METRICS_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_METRICS_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include <string>
#include <unordered_map>

#include <src/lib/fxl/macros.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/watcher.h"
#include "src/developer/memory/monitor/memory_metrics_registry.cb.h"

namespace monitor {

class Metrics {
 public:
  Metrics(zx::duration poll_frequency, async_dispatcher_t* dispatcher,
          fuchsia::cobalt::Logger_Sync* logger, memory::CaptureFn capture_cb);

 private:
  void CollectMetrics();

  zx::duration poll_frequency_;
  async_dispatcher_t* dispatcher_;
  fuchsia::cobalt::Logger_Sync* logger_;
  memory::CaptureFn capture_cb_;
  async::TaskClosureMethod<Metrics, &Metrics::CollectMetrics> task_{this};
  std::unordered_map<std::string, cobalt_registry::MemoryMetricDimensionBucket>
      bucket_name_to_code_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Metrics);
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_METRICS_H_
