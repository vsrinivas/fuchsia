// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"

#include <zircon/errors.h>

#include "lib/fidl/cpp/internal/stub.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using namespace fuchsia::cobalt;

void StubCobaltLoggerFactory::CreateLoggerFromProjectName(
    std::string project_name, ReleaseStage release_stage, fidl::InterfaceRequest<Logger> logger,
    LoggerFactory::CreateLoggerFromProjectNameCallback callback) {
  if (failure_mode_ == FAIL_CLOSE_CONNECTIONS) {
    CloseAllConnections();
    return;
  }
  if (failure_mode_ == FAIL_CREATE_LOGGER) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  logger_bindings_.AddBinding(&logger_, std::move(logger));
  callback(Status::OK);
}

void StubCobaltLoggerFactory::StubLogger::LogEvent(uint32_t metric_id, uint32_t event_code,
                                                   Logger::LogEventCallback callback) {
  if (factory_->failure_mode_ == FAIL_LOG_EVENT) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  callback(Status::OK);
}

}  // namespace feedback
