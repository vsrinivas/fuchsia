// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/internal/stub.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using fuchsia::cobalt::ReleaseStage;
using fuchsia::cobalt::Status;
using CreateLoggerFromProjectNameCallback =
    fuchsia::cobalt::LoggerFactory::CreateLoggerFromProjectNameCallback;
using fuchsia::cobalt::Logger;

void StubCobaltLoggerFactory::CreateLoggerFromProjectName(
    std::string project_name, ReleaseStage release_stage, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectNameCallback callback) {
  logger_bindings_.AddBinding(logger_.get(), std::move(logger));
  callback(Status::OK);
}

void StubCobaltLoggerFactoryClosesConnection::CreateLoggerFromProjectName(
    std::string project_name, ReleaseStage release_stage, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectNameCallback callback) {
  CloseFactoryConnection();
}

void StubCobaltLoggerFactoryFailsToCreateLogger::CreateLoggerFromProjectName(
    std::string project_name, ReleaseStage release_stage, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectNameCallback callback) {
  callback(Status::INVALID_ARGUMENTS);
}

void StubCobaltLoggerFactoryDelaysCallback::CreateLoggerFromProjectName(
    std::string project_name, ReleaseStage release_stage, fidl::InterfaceRequest<Logger> logger,
    CreateLoggerFromProjectNameCallback callback) {
  logger_bindings_.AddBinding(logger_.get(), std::move(logger));
  async::PostDelayedTask(
      dispatcher_, [cb = std::move(callback)]() { cb(Status::OK); }, delay_);
}

}  // namespace feedback
