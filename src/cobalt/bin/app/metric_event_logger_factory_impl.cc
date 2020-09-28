// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"

#include "src/cobalt/bin/app/utils.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

using config::ProjectConfigs;
using fuchsia::cobalt::Status;

constexpr uint32_t kFuchsiaCustomerId = 1;

MetricEventLoggerFactoryImpl::MetricEventLoggerFactoryImpl(CobaltServiceInterface* cobalt_service)
    : cobalt_service_(cobalt_service) {}

void MetricEventLoggerFactoryImpl::CreateMetricEventLogger(
    fuchsia::cobalt::ProjectSpec project_spec,
    fidl::InterfaceRequest<fuchsia::cobalt::MetricEventLogger> request,
    CreateMetricEventLoggerCallback callback) {
  if (shut_down_) {
    FX_LOGS(ERROR) << "The LoggerFactory received a ShutDown signal and can not "
                      "create a new Logger.";
    callback(Status::SHUT_DOWN);
    return;
  }
  uint32_t customer_id =
      project_spec.has_customer_id() ? project_spec.customer_id() : kFuchsiaCustomerId;
  auto logger = cobalt_service_->NewLogger(customer_id, project_spec.project_id());
  if (!logger) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a project with customer ID "
                   << customer_id << " and project ID " << project_spec.project_id();
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  logger_bindings_.AddBinding(std::make_unique<MetricEventLoggerImpl>(std::move(logger)),
                              std::move(request));
  callback(Status::OK);
}

void MetricEventLoggerFactoryImpl::ShutDown() {
  shut_down_ = true;
  logger_bindings_.CloseAll();
}

}  // namespace cobalt
