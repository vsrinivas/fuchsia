// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/cobalt/metrics_logger.h"

#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

#include "src/modular/bin/basemgr/cobalt/basemgr_metrics_registry.cb.h"
#include "src/modular/bin/basemgr/cobalt/metrics_impl.h"

namespace modular {
namespace {

modular::MetricsImpl* g_metrics_logger = nullptr;

}  // namespace

fit::deferred_action<fit::closure> InitializeMetricsImpl() {
  FX_DCHECK(!g_metrics_logger) << "MetricsImpl has already been initialized.";

  std::unique_ptr<modular::MetricsImpl> metrics_logger = std::make_unique<modular::MetricsImpl>(
      async_get_default_dispatcher(),
      fidl::ClientEnd<fuchsia_io::Directory>(component::OpenServiceRoot()->TakeChannel()));

  g_metrics_logger = metrics_logger.get();
  return fit::defer<fit::closure>(
      [metrics_logger = std::move(metrics_logger)] { g_metrics_logger = nullptr; });
}

void LogLifetimeEvent(
    cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType event) {
  if (g_metrics_logger) {
    g_metrics_logger->LogLifetimeEvent(event);
  }
}

void LogStoryLaunchTime(cobalt_registry::StoryLaunchTimeMigratedMetricDimensionStatus status,
                        zx::duration time) {
  if (g_metrics_logger) {
    g_metrics_logger->LogStoryLaunchTime(status, time);
  }
}

}  // namespace modular
