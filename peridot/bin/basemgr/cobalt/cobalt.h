// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_COBALT_COBALT_H_
#define PERIDOT_BIN_BASEMGR_COBALT_COBALT_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/memory/ref_ptr.h>

#include "peridot/bin/basemgr/cobalt/basemgr_metrics_registry.cb.h"

namespace modular {

// Cobalt initialization. When cobalt is not needed, the returned object must be
// deleted. This method must not be called again until then.
fit::deferred_action<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, sys::ComponentContext* context);

// Report a modular event to Cobalt.
void ReportEvent(
    cobalt_registry::ModularLifetimeEventsMetricDimensionEventType event);

// Report a module launch time duration to Cobalt.
void ReportModuleLaunchTime(std::string module_url, zx::duration time);

// Report a story launch time duration to Cobalt.
void ReportStoryLaunchTime(zx::duration time);

void ReportSessionAgentEvent(
    const std::string& url,
    cobalt_registry::SessionAgentEventsMetricDimensionEventType event);

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_COBALT_COBALT_H_
