// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_

#include <lib/fit/function.h>
#include <stdint.h>

namespace cobalt {

// An abstract interface for fetching component log statistics.
class LogStatsFetcher {
 public:
  struct Metrics {
    // This is defined as a struct to allow adding more fields.
    uint64_t error_count = 0;
  };

  // A callback that the client provides to FetchMetrics. The callback is expected to return true if
  // the metrics were successfully reported, and false if the reporting failed.
  using MetricsCallback = fit::function<bool(Metrics)>;

  virtual ~LogStatsFetcher() = default;

  virtual void FetchMetrics(MetricsCallback metrics_callback) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_H_
