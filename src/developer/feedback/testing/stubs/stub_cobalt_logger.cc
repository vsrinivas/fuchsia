// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"

namespace feedback {

using fuchsia::cobalt::Status;
using LogEventCallback = fuchsia::cobalt::Logger::LogEventCallback;

void StubCobaltLogger::LogEvent(uint32_t metric_id, uint32_t event_code,
                                LogEventCallback callback) {
  MarkLogEventAsCalled();
  SetLastEvent(metric_id, event_code);
  callback(Status::OK);
}

void StubCobaltLogger::LogEventCount(uint32_t metric_id, uint32_t event_code,
                                     ::std::string component, int64_t period_duration_micros,
                                     int64_t count,
                                     fuchsia::cobalt::Logger::LogEventCountCallback callback) {
  MarkLogEventCountAsCalled();
  SetLastEvent(metric_id, event_code, count);
  callback(Status::OK);
}

void StubCobaltLoggerFailsLogEvent::LogEvent(uint32_t metric_id, uint32_t event_code,
                                             LogEventCallback callback) {
  callback(Status::INVALID_ARGUMENTS);
}

void StubCobaltLoggerIgnoresFirstEvents::LogEvent(
    uint32_t metric_id, uint32_t event_code, fuchsia::cobalt::Logger::LogEventCallback callback) {
  ++num_calls_;
  if (num_calls_ <= to_ignore_) {
    return;
  }
  MarkLogEventAsCalled();
  SetLastEvent(metric_id, event_code);
  callback(Status::OK);
}

}  // namespace feedback
