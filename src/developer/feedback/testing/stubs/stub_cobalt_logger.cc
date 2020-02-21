// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"

namespace feedback {
namespace {

using fuchsia::cobalt::Status;
using LogEventCallback = fuchsia::cobalt::Logger::LogEventCallback;

CobaltEventType DetermineCobaltEventType(uint32_t metric_id, uint32_t event_code) {
  // This swtich statement needs to be updated if any new count or time elapsed metrics are added.
  // We need to use a single event code of each event type as a canary to get the metric id.
  switch (metric_id) {
    case MetricIDForEventCode(BugreportGenerationFlow::kUnknown):
      return CobaltEventType::kTimeElapsed;
    case MetricIDForEventCode(UploadAttemptState::kUnknown):
      return CobaltEventType::kCount;
    case MetricIDForEventCode(RebootReason::kOOM):
    case MetricIDForEventCode(CrashState::kUnknown):
    case MetricIDForEventCode(CrashpadFunctionError::kUnknown):
    case MetricIDForEventCode(TimedOutData::kUnknown):
    default:
      return CobaltEventType::kOccurrence;
  }
}

}  // namespace

void StubCobaltLoggerBase::SetLastEvent(uint32_t metric_id, uint32_t event_code, uint64_t count) {
  events_.push_back(
      CobaltEvent(DetermineCobaltEventType(metric_id, event_code), metric_id, event_code, count));
}

void StubCobaltLogger::LogEvent(uint32_t metric_id, uint32_t event_code,
                                LogEventCallback callback) {
  MarkLogEventAsCalled();
  SetLastEvent(metric_id, event_code, /*count=*/0);
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

void StubCobaltLogger::LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                                      ::std::string component, int64_t elapsed_micros,
                                      fuchsia::cobalt::Logger::LogEventCountCallback callback) {
  MarkLogElapsedTimeAsCalled();
  SetLastEvent(metric_id, event_code, elapsed_micros);
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
  SetLastEvent(metric_id, event_code, /*count=*/0);
  callback(Status::OK);
}

}  // namespace feedback
