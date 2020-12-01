// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_ARCHIVIST_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_ARCHIVIST_STATS_FETCHER_H_

#include <lib/fit/function.h>
#include <lib/fit/optional.h>
#include <stdint.h>

#include <algorithm>

#include "src/cobalt/bin/system-metrics/diagnostics_metrics_registry.cb.h"

namespace cobalt {

// An abstract interface for fetching archivist statistics.
class ArchivistStatsFetcher {
 public:
  class MeasurementKey {
   public:
    MeasurementKey(uint32_t metric_id, std::vector<uint32_t> event_codes)
        : metric_id_(metric_id), event_codes_(std::move(event_codes)) {}

    uint32_t metric_id() const { return metric_id_; }
    const std::vector<uint32_t> event_codes() const { return event_codes_; }

    bool operator<(const MeasurementKey& other) const {
      if (metric_id_ != other.metric_id_) {
        return metric_id_ < other.metric_id_;
      }
      return event_codes_ < other.event_codes_;
    }

   private:
    uint32_t metric_id_;
    std::vector<uint32_t> event_codes_;
  };

  using MeasurementValue = uint64_t;
  using Measurement = std::pair<MeasurementKey, MeasurementValue>;

  // A callback that the client provides to FetchMetrics. The callback is expected to return true if
  // the metrics were successfully reported, and false if the reporting failed.
  using MetricsCallback = fit::function<bool(const Measurement&)>;

  virtual ~ArchivistStatsFetcher() = default;

  virtual void FetchMetrics(MetricsCallback metrics_callback) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_ARCHIVIST_STATS_FETCHER_H_
