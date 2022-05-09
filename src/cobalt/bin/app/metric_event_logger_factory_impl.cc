// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"

#include "fuchsia/metrics/cpp/fidl.h"
#include "src/cobalt/bin/app/utils.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

constexpr uint32_t kFuchsiaCustomerId = 1;

MetricEventLoggerFactoryImpl::MetricEventLoggerFactoryImpl(CobaltServiceInterface* cobalt_service)
    : cobalt_service_(cobalt_service) {}

void MetricEventLoggerFactoryImpl::CreateMetricEventLogger(
    fuchsia::metrics::ProjectSpec project_spec,
    fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> request,
    CreateMetricEventLoggerCallback callback) {
  std::vector<uint32_t> experiment_ids = std::vector<uint32_t>();
  callback(DoCreateMetricEventLogger(std::move(project_spec), std::move(experiment_ids),
                                     std::move(request)));
}

void MetricEventLoggerFactoryImpl::CreateMetricEventLoggerWithExperiments(
    fuchsia::metrics::ProjectSpec project_spec, std::vector<uint32_t> experiment_ids,
    fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> request,
    CreateMetricEventLoggerWithExperimentsCallback callback) {
  callback(DoCreateMetricEventLogger(std::move(project_spec), std::move(experiment_ids),
                                     std::move(request)));
}

fpromise::result<void, fuchsia::metrics::Error>
MetricEventLoggerFactoryImpl::DoCreateMetricEventLogger(
    fuchsia::metrics::ProjectSpec project_spec, std::vector<uint32_t> experiment_ids,
    fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> request) {
  if (shut_down_) {
    FX_LOGS(ERROR) << "The LoggerFactory received a ShutDown signal and can not "
                      "create a new Logger.";
    return fpromise::error(fuchsia::metrics::Error::SHUT_DOWN);
  }
  uint32_t customer_id =
      project_spec.has_customer_id() ? project_spec.customer_id() : kFuchsiaCustomerId;
  auto logger =
      cobalt_service_->NewLogger(customer_id, project_spec.project_id(), std::move(experiment_ids));
  if (!logger) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a project with customer ID "
                   << customer_id << " and project ID " << project_spec.project_id();
    return fpromise::error(fuchsia::metrics::Error::INVALID_ARGUMENTS);
  }
  logger_bindings_.AddBinding(std::make_unique<MetricEventLoggerImpl>(std::move(logger)),
                              std::move(request));
  return fpromise::ok();
}

void MetricEventLoggerFactoryImpl::ShutDown() {
  shut_down_ = true;
  logger_bindings_.CloseAll();
}

}  // namespace cobalt
