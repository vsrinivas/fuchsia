// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/internal/stub.h>
#include <zircon/errors.h>

#include "fuchsia/cobalt/cpp/fidl.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using fuchsia::cobalt::Status;
using CreateLoggerFromProjectIdCallback =
    fuchsia::cobalt::LoggerFactory::CreateLoggerFromProjectIdCallback;
using fuchsia::cobalt::Logger;

void StubCobaltLoggerFactoryBase::CloseFactoryConnection() {
  if (factory_binding_) {
    factory_binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void StubCobaltLoggerFactoryBase::CloseLoggerConnection() {
  if (logger_binding_) {
    logger_binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void StubCobaltLoggerFactoryBase::CloseAllConnections() {
  CloseFactoryConnection();
  CloseLoggerConnection();
}

void StubCobaltLoggerFactory::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectIdCallback callback) {
  logger_binding_ =
      std::make_unique<fidl::Binding<fuchsia::cobalt::Logger>>(logger_.get(), std::move(logger));
  callback(Status::OK);
}

void StubCobaltLoggerFactoryClosesConnection::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectIdCallback callback) {
  CloseFactoryConnection();
}

void StubCobaltLoggerFactoryFailsToCreateLogger::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectIdCallback callback) {
  callback(Status::INVALID_ARGUMENTS);
}

void StubCobaltLoggerFactoryCreatesOnRetry::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectIdCallback callback) {
  ++num_calls_;
  if (num_calls_ >= succeed_after_) {
    logger_binding_ =
        std::make_unique<fidl::Binding<fuchsia::cobalt::Logger>>(logger_.get(), std::move(logger));
    callback(Status::OK);
    return;
  }

  callback(Status::INVALID_ARGUMENTS);
}

void StubCobaltLoggerFactoryDelaysCallback::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectIdCallback callback) {
  logger_binding_ =
      std::make_unique<fidl::Binding<fuchsia::cobalt::Logger>>(logger_.get(), std::move(logger));
  async::PostDelayedTask(
      dispatcher_, [cb = std::move(callback)]() { cb(Status::OK); }, delay_);
}

}  // namespace feedback
