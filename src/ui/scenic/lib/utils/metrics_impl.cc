// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/metrics_impl.h"

#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>

namespace metrics {

using fuchsia_metrics::MetricEventLogger;
using fuchsia_metrics::MetricEventLoggerFactory;

using MetricsResult = ::fit::result<fidl::internal::ErrorsInImpl<fuchsia_metrics::Error>>;

MetricsImpl::MetricsImpl(async_dispatcher_t* dispatcher,
                         fidl::ClientEnd<fuchsia_io::Directory> directory)
    : ServiceHubConnector(dispatcher), directory_(std::move(directory)) {}

void MetricsImpl::LogRareEvent(cobalt_registry::ScenicRareEventMigratedMetricDimensionEvent event) {
  Do([event](fidl::Client<MetricEventLogger>& logger, DoResolver resolver) {
    logger->LogOccurrence({cobalt_registry::kScenicRareEventMigratedMetricId, 1, {event}})
        .Then([resolver = std::move(resolver)](MetricsResult result) mutable {
          resolver.resolve(result.is_error() && (result.error_value().is_framework_error() ||
                                                 result.error_value().domain_error() ==
                                                     fuchsia_metrics::Error::kBufferFull));
        });
  });
}

void MetricsImpl::LogLatchToActualPresentation(
    std::optional<
        cobalt_registry::ScenicLatchToActualPresentationMigratedMetricDimensionFrameStatus>
        frame_status,
    std::vector<fuchsia_metrics::HistogramBucket> histogram) {
  Do([frame_status, histogram = std::move(histogram)](fidl::Client<MetricEventLogger>& logger,
                                                      DoResolver resolver) mutable {
    uint32_t frame_status_value = 0;
    if (frame_status) {
      frame_status_value = frame_status.value();
    }
    logger
        ->LogIntegerHistogram({cobalt_registry::kScenicLatchToActualPresentationMigratedMetricId,
                               std::move(histogram),
                               {frame_status_value}})
        .Then([resolver = std::move(resolver)](MetricsResult result) mutable {
          resolver.resolve(result.is_error() && (result.error_value().is_framework_error() ||
                                                 result.error_value().domain_error() ==
                                                     fuchsia_metrics::Error::kBufferFull));
        });
  });
}

void MetricsImpl::ConnectToServiceHub(ServiceHubConnectResolver resolver) {
  auto connection = component::ConnectAt<MetricEventLoggerFactory>(directory_);
  if (connection.is_ok()) {
    resolver.resolve(std::move(connection.value()));
  }
}

void MetricsImpl::ConnectToService(fidl::Client<MetricEventLoggerFactory>& factory,
                                   ServiceConnectResolver resolver) {
  auto endpoints = fidl::CreateEndpoints<MetricEventLogger>();

  factory
      ->CreateMetricEventLogger(
          {fuchsia_metrics::ProjectSpec({.project_id = cobalt_registry::kProjectId}),
           std::move(endpoints->server)})
      .Then([resolver = std::move(resolver), client_end = std::move(endpoints->client)](
                fidl::Result<MetricEventLoggerFactory::CreateMetricEventLogger>& response) mutable {
        if (response.is_ok()) {
          resolver.resolve(std::move(client_end));
        }
      });
}

}  // namespace metrics
