// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/cobalt/metrics_impl.h"

#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>

namespace modular {

using fuchsia_metrics::MetricEventLogger;
using fuchsia_metrics::MetricEventLoggerFactory;

using MetricsResult = ::fit::result<fidl::internal::ErrorsInImpl<fuchsia_metrics::Error>>;

MetricsImpl::MetricsImpl(async_dispatcher_t* dispatcher,
                         fidl::ClientEnd<fuchsia_io::Directory> directory)
    : ServiceHubConnector(dispatcher), directory_(std::move(directory)) {}

void MetricsImpl::LogLifetimeEvent(
    cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType event) {
  Do([event](fidl::Client<MetricEventLogger>& logger, DoResolver resolver) {
    logger->LogOccurrence({cobalt_registry::kModularLifetimeEventsMigratedMetricId, 1, {event}})
        .Then([resolver = std::move(resolver)](MetricsResult result) mutable {
          resolver.resolve(result.is_error() && (result.error_value().is_framework_error() ||
                                                 result.error_value().domain_error() ==
                                                     fuchsia_metrics::Error::kBufferFull));
        });
  });
}

void MetricsImpl::LogStoryLaunchTime(
    cobalt_registry::StoryLaunchTimeMigratedMetricDimensionStatus status, zx::duration time) {
  Do([status, time](fidl::Client<MetricEventLogger>& logger, DoResolver resolver) mutable {
    uint32_t status_value = 0;
    if (status) {
      status_value = status;
    }
    logger
        ->LogInteger(
            {cobalt_registry::kStoryLaunchTimeMigratedMetricId, time.to_usecs(), {status_value}})
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

}  // namespace modular
