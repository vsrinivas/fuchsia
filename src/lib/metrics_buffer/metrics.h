// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_METRICS_BUFFER_METRICS_H_
#define SRC_LIB_METRICS_BUFFER_METRICS_H_

#include <vector>

#include "fidl/fuchsia.metrics/cpp/fidl.h"

namespace cobalt {

class Metrics {
 public:
  // Log metric events to Cobalt.
  virtual void LogMetricEvents(std::vector<fuchsia_metrics::MetricEvent> events) = 0;
};

}  // namespace cobalt

#endif  // SRC_LIB_METRICS_BUFFER_METRICS_H_
