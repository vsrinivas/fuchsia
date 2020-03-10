// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"

#include "src/cobalt/bin/app/utils.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

using cobalt::logger::ProjectContextFactory;
using config::ProjectConfigs;
using fuchsia::cobalt::Status;

constexpr uint32_t kFuchsiaCustomerId = 1;

MetricEventLoggerFactoryImpl::MetricEventLoggerFactoryImpl(
    std::shared_ptr<cobalt::logger::ProjectContextFactory> global_project_context_factory,
    CobaltServiceInterface* cobalt_service)
    : global_project_context_factory_(std::move(global_project_context_factory)),
      cobalt_service_(cobalt_service) {}

void MetricEventLoggerFactoryImpl::CreateMetricEventLogger(
    fuchsia::cobalt::ProjectSpec project_spec,
    fidl::InterfaceRequest<fuchsia::cobalt::MetricEventLogger> request,
    CreateMetricEventLoggerCallback callback) {
  uint32_t customer_id =
      project_spec.has_customer_id() ? project_spec.customer_id() : kFuchsiaCustomerId;
  auto project_context =
      global_project_context_factory_->NewProjectContext(customer_id, project_spec.project_id());
  if (!project_context) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a project with customer ID "
                   << customer_id << " and project ID " << project_spec.project_id();
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  logger_bindings_.AddBinding(std::make_unique<MetricEventLoggerImpl>(
                                  cobalt_service_->NewLogger(std::move(project_context))),
                              std::move(request));
  callback(Status::OK);
}

}  // namespace cobalt
