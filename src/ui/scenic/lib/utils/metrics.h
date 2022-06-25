// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_METRICS_H_
#define SRC_UI_SCENIC_LIB_UTILS_METRICS_H_

#include "fidl/fuchsia.metrics/cpp/natural_types.h"
#include "src/lib/fidl/cpp/contrib/connection/service_hub_connector.h"
#include "src/ui/scenic/lib/scheduling/frame_metrics_registry.cb.h"

namespace metrics {

class Metrics {
 public:
  virtual void LogRareEvent(cobalt_registry::ScenicRareEventMigratedMetricDimensionEvent event) = 0;
  virtual void LogLatchToActualPresentation(
      std::optional<
          cobalt_registry::ScenicLatchToActualPresentationMigratedMetricDimensionFrameStatus>
          frame_status,
      std::vector<fuchsia_metrics::HistogramBucket> histogram) = 0;
};

}  // namespace metrics

#endif  // SRC_UI_SCENIC_LIB_UTILS_METRICS_H_
