// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.cobalt/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <utility>

#include <cobalt-client/cpp/collector_internal.h>
#include <cobalt-client/cpp/histogram_internal.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {
namespace internal {
namespace {

fuchsia_cobalt::wire::CobaltEvent MetricIntoToCobaltEvent(const MetricOptions& metric_info) {
  fuchsia_cobalt::wire::CobaltEvent event;
  event.metric_id = metric_info.metric_id;
  event.component = fidl::StringView::FromExternal(metric_info.component);
  // Safe to do so, since the request is read only.
  event.event_codes = fidl::VectorView<uint32_t>::FromExternal(
      const_cast<uint32_t*>(metric_info.event_codes.data()), metric_info.metric_dimensions);
  return event;
}

}  // namespace

std::string_view CobaltLogger::GetServiceName() {
  return fidl::DiscoverableProtocolName<fuchsia_cobalt::LoggerFactory>;
}

bool CobaltLogger::TryObtainLogger() {
  if (logger_.client_end().is_valid()) {
    return true;
  }

  auto factory_endpoints = fidl::CreateEndpoints<fuchsia_cobalt::LoggerFactory>();
  if (factory_endpoints.is_error()) {
    return false;
  }
  auto [logger_factory_client, logger_factory_srv] = *std::move(factory_endpoints);

  if (options_.service_connect(options_.service_path.c_str(), logger_factory_srv.TakeChannel()) !=
      ZX_OK) {
    return false;
  }

  // Obtain a logger service for the project ID.
  auto logger_endpoints = fidl::CreateEndpoints<fuchsia_cobalt::Logger>();
  if (logger_endpoints.is_error()) {
    return false;
  }
  auto [logger_client, logger_server] = *std::move(logger_endpoints);
  auto create_logger_result =
      fidl::WireCall(logger_factory_client)
          .CreateLoggerFromProjectId(options_.project_id, std::move(logger_server));
  if (create_logger_result.status() == ZX_OK &&
      create_logger_result->status == fuchsia_cobalt::wire::Status::kOk) {
    logger_ = fidl::BindSyncClient(std::move(logger_client));
    return true;
  }
  return false;
}

bool CobaltLogger::Log(const MetricOptions& metric_info, const HistogramBucket* buckets,
                       size_t bucket_count) {
  if (!TryObtainLogger()) {
    return false;
  }
  auto event = MetricIntoToCobaltEvent(metric_info);
  // Safe because is read only.
  auto int_histogram = fidl::VectorView<HistogramBucket>::FromExternal(
      const_cast<HistogramBucket*>(buckets), bucket_count);
  event.payload.set_int_histogram(
      fidl::ObjectView<fidl::VectorView<HistogramBucket>>::FromExternal(&int_histogram));

  auto log_result = logger_.LogCobaltEvent(std::move(event));
  if (log_result.status() == ZX_ERR_PEER_CLOSED) {
    Reset();
  }
  return log_result.status() == ZX_OK && log_result->status == fuchsia_cobalt::wire::Status::kOk;
}

bool CobaltLogger::Log(const MetricOptions& metric_info, RemoteCounter::Type count) {
  if (!TryObtainLogger()) {
    return false;
  }
  auto event = MetricIntoToCobaltEvent(metric_info);
  fuchsia_cobalt::wire::CountEvent event_count{.period_duration_micros = 0, .count = count};
  event.payload.set_event_count(
      fidl::ObjectView<fuchsia_cobalt::wire::CountEvent>::FromExternal(&event_count));

  auto log_result = logger_.LogCobaltEvent(std::move(event));
  if (log_result.status() == ZX_ERR_PEER_CLOSED) {
    Reset();
  }
  return log_result.status() == ZX_OK && log_result->status == fuchsia_cobalt::wire::Status::kOk;
}

bool CobaltLogger::LogInteger(const MetricOptions& metric_info, RemoteCounter::Type value) {
  if (!TryObtainLogger()) {
    return false;
  }

  auto event = MetricIntoToCobaltEvent(metric_info);
  event.payload.set_memory_bytes_used(fidl::ObjectView<RemoteCounter::Type>::FromExternal(&value));

  // Cobalt 1.0 does not support integer. The closest to integer is memory
  // usage. So, we use MemoryUsage until we have a better support for integer(in
  // version 1.1).
  auto log_result =
      logger_.LogMemoryUsage(event.metric_id, event.event_codes[0], event.component, value);
  if (log_result.status() == ZX_ERR_PEER_CLOSED) {
    Reset();
  }
  return log_result.status() == ZX_OK && log_result->status == fuchsia_cobalt::wire::Status::kOk;
}

}  // namespace internal
}  // namespace cobalt_client
