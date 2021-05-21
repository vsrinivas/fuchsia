// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"

#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/internal/stub.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace stubs {

using fuchsia::metrics::MetricEventLogger;
using fuchsia::metrics::Status;

void CobaltLoggerFactoryBase::CloseLoggerConnection() {
  if (logger_binding_) {
    logger_binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void CobaltLoggerFactory::CreateMetricEventLogger(
    ::fuchsia::metrics::ProjectSpec project_spec,
    ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
    CreateMetricEventLoggerCallback callback) {
  logger_binding_ = std::make_unique<::fidl::Binding<fuchsia::metrics::MetricEventLogger>>(
      logger_.get(), std::move(logger));
  callback(Status::OK);
}

void CobaltLoggerFactoryFailsToCreateLogger::CreateMetricEventLogger(
    ::fuchsia::metrics::ProjectSpec project_spec,
    ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
    CreateMetricEventLoggerCallback callback) {
  callback(Status::INVALID_ARGUMENTS);
}

void CobaltLoggerFactoryCreatesOnRetry::CreateMetricEventLogger(
    ::fuchsia::metrics::ProjectSpec project_spec,
    ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
    CreateMetricEventLoggerCallback callback) {
  ++num_calls_;
  if (num_calls_ >= succeed_after_) {
    logger_binding_ = std::make_unique<::fidl::Binding<fuchsia::metrics::MetricEventLogger>>(
        logger_.get(), std::move(logger));
    callback(Status::OK);
    return;
  }

  callback(Status::INVALID_ARGUMENTS);
}

void CobaltLoggerFactoryDelaysCallback::CreateMetricEventLogger(
    ::fuchsia::metrics::ProjectSpec project_spec,
    ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
    CreateMetricEventLoggerCallback callback) {
  logger_binding_ = std::make_unique<::fidl::Binding<fuchsia::metrics::MetricEventLogger>>(
      logger_.get(), std::move(logger));
  async::PostDelayedTask(
      dispatcher_, [cb = std::move(callback)]() { cb(Status::OK); }, delay_);
}

}  // namespace stubs
}  // namespace forensics
