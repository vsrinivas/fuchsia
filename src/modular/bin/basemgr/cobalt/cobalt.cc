// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/cobalt/cobalt.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/modular/bin/basemgr/cobalt/basemgr_metrics_registry.cb.h"

namespace modular {
namespace {

cobalt::CobaltLogger* g_cobalt_logger = nullptr;

}  // namespace

fit::deferred_action<fit::closure> InitializeCobalt(async_dispatcher_t* dispatcher,
                                                    sys::ComponentContext* context) {
  FX_DCHECK(!g_cobalt_logger) << "Cobalt has already been initialized.";

  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger =
      cobalt::NewCobaltLoggerFromProjectId(dispatcher, context->svc(), cobalt_registry::kProjectId);

  g_cobalt_logger = cobalt_logger.get();
  return fit::defer<fit::closure>(
      [cobalt_logger = std::move(cobalt_logger)] { g_cobalt_logger = nullptr; });
}

void ReportEvent(cobalt_registry::ModularLifetimeEventsMetricDimensionEventType event) {
  if (g_cobalt_logger) {
    g_cobalt_logger->LogEvent(
        static_cast<uint32_t>(cobalt_registry::kModularLifetimeEventsMetricId),
        static_cast<uint32_t>(event));
  }
}

void ReportStoryLaunchTime(zx::duration time) {
  if (g_cobalt_logger) {
    g_cobalt_logger->LogElapsedTime(
        static_cast<uint32_t>(cobalt_registry::kStoryLaunchTimeMetricId), 0, "", time);
  }
}

}  // namespace modular
