// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_IMPL_H_

#include "fidl/fuchsia.io/cpp/markers.h"
#include "fidl/fuchsia.metrics/cpp/fidl.h"
#include "src/lib/fidl/cpp/contrib/connection/service_hub_connector.h"
#include "src/modular/bin/basemgr/cobalt/basemgr_metrics_registry.cb.h"
#include "src/modular/bin/basemgr/cobalt/metrics.h"

namespace modular {

// This class connects to the MetricsEventLoggerFactory and MetricsEventLogger fidl endpoints using
// ServiceHubConnector. We are using ServiceHubConnector to handle fidl endpoint reconnects
// and fidl call retries.
//
// TODO(b/249376344): Remove this class when the functionality of ServiceHubConnector is built into
// fidl api call.
class MetricsImpl final
    : public Metrics,
      private fidl::contrib::ServiceHubConnector<fuchsia_metrics::MetricEventLoggerFactory,
                                                 fuchsia_metrics::MetricEventLogger> {
 public:
  explicit MetricsImpl(async_dispatcher_t* dispatcher,
                       fidl::ClientEnd<fuchsia_io::Directory> directory);
  // Log a modular event to Cobalt.
  void LogLifetimeEvent(
      cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType event) override;

  // Log a story launch time duration to Cobalt.
  void LogStoryLaunchTime(cobalt_registry::StoryLaunchTimeMigratedMetricDimensionStatus status,
                          zx::duration time) override;

 private:
  void ConnectToServiceHub(ServiceHubConnectResolver resolver) override;
  void ConnectToService(fidl::Client<fuchsia_metrics::MetricEventLoggerFactory>& factory,
                        ServiceConnectResolver resolver) override;

  fidl::ClientEnd<fuchsia_io::Directory> directory_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_COBALT_METRICS_IMPL_H_
