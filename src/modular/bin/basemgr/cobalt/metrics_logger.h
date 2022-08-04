// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_LOGGER_H_
#define SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_LOGGER_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/modular/bin/basemgr/cobalt/basemgr_metrics_registry.cb.h"

namespace modular {

// MetricsImpl initialization. When cobalt is not needed, the returned object must be
// deleted. This method must not be called again until then.
[[nodiscard]] fit::deferred_action<fit::closure> InitializeMetricsImpl();

// Log a modular event to Cobalt.
void LogLifetimeEvent(cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType event);

// Log a story launch time duration to Cobalt.
void LogStoryLaunchTime(cobalt_registry::StoryLaunchTimeMigratedMetricDimensionStatus status,
                        zx::duration time);

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_LOGGER_H_
