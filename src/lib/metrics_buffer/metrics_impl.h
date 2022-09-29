// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_METRICS_BUFFER_METRICS_IMPL_H_
#define SRC_LIB_METRICS_BUFFER_METRICS_IMPL_H_

#include "fidl/fuchsia.io/cpp/markers.h"
#include "fidl/fuchsia.metrics/cpp/fidl.h"
#include "src/lib/fidl/cpp/contrib/connection/service_hub_connector.h"
#include "src/lib/metrics_buffer/metrics.h"

namespace cobalt {

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
                       fidl::ClientEnd<fuchsia_io::Directory> directory, uint32_t project_id);

  void LogMetricEvents(std::vector<fuchsia_metrics::MetricEvent> events) override;

 private:
  void ConnectToServiceHub(ServiceHubConnectResolver resolver) override;
  void ConnectToService(fidl::Client<fuchsia_metrics::MetricEventLoggerFactory>& factory,
                        ServiceConnectResolver resolver) override;

  fidl::ClientEnd<fuchsia_io::Directory> directory_;
  const uint32_t project_id_{};
};

}  // namespace cobalt

#endif  // SRC_LIB_METRICS_BUFFER_METRICS_IMPL_H_
