// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/metrics_buffer/metrics_impl.h"

#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>

namespace cobalt {

using fuchsia_metrics::MetricEventLogger;
using fuchsia_metrics::MetricEventLoggerFactory;

using MetricsResult = ::fit::result<fidl::internal::AnyErrorInImpl<fuchsia_metrics::Error>>;

MetricsImpl::MetricsImpl(async_dispatcher_t* dispatcher,
                         fidl::ClientEnd<fuchsia_io::Directory> directory, uint32_t project_id)
    : ServiceHubConnector(dispatcher), directory_(std::move(directory)), project_id_(project_id) {}

void MetricsImpl::LogMetricEvents(std::vector<fuchsia_metrics::MetricEvent> events) {
  Do([events = std::move(events)](fidl::Client<MetricEventLogger>& logger, DoResolver resolver) {
    logger->LogMetricEvents({events}).Then(
        [resolver = std::move(resolver)](MetricsResult result) mutable {
          // Should retry if the result returns an error and the error is either a transport error
          // of the request message or logger's local buffer is temporarily full.
          resolver.resolve(result.is_error() && (result.error_value().is_transport_error() ||
                                                 result.error_value().application_error() ==
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
          {fuchsia_metrics::ProjectSpec({.project_id = project_id_}), std::move(endpoints->server)})
      .Then([resolver = std::move(resolver), client_end = std::move(endpoints->client)](
                fidl::Result<MetricEventLoggerFactory::CreateMetricEventLogger>& response) mutable {
        if (response.is_ok()) {
          resolver.resolve(std::move(client_end));
        }
      });
}

}  // namespace cobalt
