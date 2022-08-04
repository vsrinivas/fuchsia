// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_H_
#define SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_H_

#include "fidl/fuchsia.metrics/cpp/natural_types.h"
#include "src/modular/bin/basemgr/cobalt/basemgr_metrics_registry.cb.h"

namespace modular {

class Metrics {
 public:
  // Log a modular event to Cobalt.
  virtual void LogLifetimeEvent(
      cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType event) = 0;

  // Log a story launch time duration to Cobalt.
  virtual void LogStoryLaunchTime(
      cobalt_registry::StoryLaunchTimeMigratedMetricDimensionStatus status, zx::duration time) = 0;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_H_
