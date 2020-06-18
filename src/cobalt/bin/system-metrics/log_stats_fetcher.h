// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_

#include <lib/fit/function.h>
#include <stdint.h>

#include <unordered_map>

#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"

namespace cobalt {

using ComponentEventCode =
    fuchsia_system_metrics::PerComponentErrorLogCountMetricDimensionComponent;

// An abstract interface for fetching component log statistics.
class LogStatsFetcher {
 public:
  struct Metrics {
    // The number of new error logs across all components since the last call to
    // FetchMetrics().
    uint64_t error_count = 0;

    // A map from component event codes (as defined in metrics.yaml) to the
    // number of error logs since the last call to FetchMetrics().
    std::unordered_map<ComponentEventCode, uint64_t> per_component_error_count;
  };

  using MetricsCallback = fit::function<void(const Metrics&)>;

  virtual ~LogStatsFetcher() = default;

  virtual void FetchMetrics(MetricsCallback metrics_callback) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_
