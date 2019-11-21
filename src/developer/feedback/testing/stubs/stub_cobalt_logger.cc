// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"

namespace feedback {

using fuchsia::cobalt::Status;
using LogEventCallback = fuchsia::cobalt::Logger::LogEventCallback;

void StubCobaltLogger::LogEvent(uint32_t metric_id, uint32_t event_code,
                                LogEventCallback callback) {
  SetLastMetricIdAndEventCode(metric_id, event_code);
  MarkLogEventAsCalled();
  callback(Status::OK);
}

void StubCobaltLoggerFailsLogEvent::LogEvent(uint32_t metric_id, uint32_t event_code,
                                             LogEventCallback callback) {
  callback(Status::INVALID_ARGUMENTS);
}

}  // namespace feedback
