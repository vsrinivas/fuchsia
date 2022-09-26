// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_METRICS_METRICS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_METRICS_METRICS_H_

#include <vector>

#include "fidl/fuchsia.metrics/cpp/fidl.h"

namespace media::audio {

class Metrics {
 public:
  // Log a metric events to Cobalt.
  virtual void LogMetricEvents(std::vector<fuchsia_metrics::MetricEvent> events) = 0;

  // Log an integer histogram to Cobalt.
  virtual void LogIntegerHistogram(uint32_t metric_id,
                                   std::vector<fuchsia_metrics::HistogramBucket> histogram,
                                   std::vector<uint32_t> event_codes) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_METRICS_METRICS_H_
