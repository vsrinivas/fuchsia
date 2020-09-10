// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_

#include <lib/fit/function.h>
#include <stdint.h>

#include <string>
#include <unordered_map>

#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"

namespace cobalt {

using ComponentEventCode =
    fuchsia_system_metrics::PerComponentErrorLogCountMetricDimensionComponent;

// An abstract interface for fetching component log statistics.
class LogStatsFetcher {
 public:
  struct GranularStatsRecord {
    std::string file_path;
    uint64_t line_no;
    uint64_t count;

    GranularStatsRecord(const std::string& file_path, uint64_t line_no, uint64_t count)
        : file_path(file_path), line_no(line_no), count(count) {}

    bool operator==(const GranularStatsRecord& other) const {
      return file_path == other.file_path && line_no == other.line_no && count == other.count;
    }
  };

  struct Metrics {
    // The number of new error logs across all components + klog since the last call to
    // FetchMetrics().
    uint64_t error_count = 0;

    // The number of new kernel logs since the last call to FetchMetrics().
    uint64_t klog_count = 0;

    // A map from component event codes (as defined in metrics.yaml) to the
    // number of error logs since the last call to FetchMetrics(). Errors
    // that don't belong to components in the allowlist will be reported with
    // "Other" as event code.
    std::unordered_map<ComponentEventCode, uint64_t> per_component_error_count;

    // Contains the counts of all error logs that occured since the last report. Each record
    // contains the file path and line number corresponding to where the error log had
    // originated.
    std::vector<GranularStatsRecord> granular_stats;
  };

  using MetricsCallback = fit::function<void(const Metrics&)>;

  virtual ~LogStatsFetcher() = default;

  virtual void FetchMetrics(MetricsCallback metrics_callback) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_
