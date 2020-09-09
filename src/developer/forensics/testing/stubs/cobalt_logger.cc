// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/cobalt_logger.h"

namespace forensics {
namespace stubs {
namespace {

using fuchsia::cobalt::Status;
using LogEventCallback = fuchsia::cobalt::Logger::LogEventCallback;

cobalt::EventType DetermineCobaltEventType(uint32_t metric_id, uint32_t event_code) {
  // This swtich statement needs to be updated if any new count or time elapsed metrics are added.
  // We need to use a single event code of each event type as a canary to get the metric id.
  switch (metric_id) {
    case MetricIDForEventCode(cobalt::RebootReasonWriteResult::kSuccess):
    case MetricIDForEventCode(cobalt::LastRebootReason::kUnknown):
    case MetricIDForEventCode(cobalt::SnapshotGenerationFlow::kUnknown):
      return cobalt::EventType::kTimeElapsed;
    case MetricIDForEventCode(cobalt::UploadAttemptState::kUnknown):
    case MetricIDForEventCode(cobalt::SnapshotVersion::kUnknown):
      return cobalt::EventType::kCount;
    case MetricIDForEventCode(cobalt::PreviousBootEncodingVersion::kUnknown):
      return cobalt::EventType::kCount;
    case MetricIDForEventCode(cobalt::LegacyRebootReason::kOOM):
    case MetricIDForEventCode(cobalt::CrashState::kUnknown):
    case MetricIDForEventCode(cobalt::CrashpadFunctionError::kUnknown):
    case MetricIDForEventCode(cobalt::TimedOutData::kUnknown):
    default:
      return cobalt::EventType::kOccurrence;
  }
}

}  // namespace

void CobaltLoggerBase::SetLastEvent(uint32_t metric_id, uint32_t event_code, uint64_t count) {
  events_.push_back(cobalt::Event(DetermineCobaltEventType(metric_id, event_code), metric_id,
                                  {event_code}, count));
}

void CobaltLoggerBase::SetLastEvent(uint32_t metric_id, std::vector<uint32_t> event_codes,
                                    uint64_t count) {
  events_.push_back(
      cobalt::Event(cobalt::EventType::kMultidimensionalOccurrence, metric_id, event_codes, count));
}

void CobaltLogger::LogEvent(uint32_t metric_id, uint32_t event_code, LogEventCallback callback) {
  MarkLogEventAsCalled();
  SetLastEvent(metric_id, event_code, /*count=*/0);
  callback(Status::OK);
}

void CobaltLogger::LogEventCount(uint32_t metric_id, uint32_t event_code, ::std::string component,
                                 int64_t period_duration_micros, int64_t count,
                                 fuchsia::cobalt::Logger::LogEventCountCallback callback) {
  MarkLogEventCountAsCalled();
  SetLastEvent(metric_id, event_code, count);
  callback(Status::OK);
}

void CobaltLogger::LogElapsedTime(uint32_t metric_id, uint32_t event_code, ::std::string component,
                                  int64_t elapsed_micros,
                                  fuchsia::cobalt::Logger::LogEventCountCallback callback) {
  MarkLogElapsedTimeAsCalled();
  SetLastEvent(metric_id, event_code, elapsed_micros);
  callback(Status::OK);
}

void CobaltLogger::LogCobaltEvent(fuchsia::cobalt::CobaltEvent event,
                                  fuchsia::cobalt::Logger::LogCobaltEventCallback callback) {
  MarkLogCobaltEventAsCalled();
  SetLastEvent(event.metric_id, event.event_codes, event.payload.event_count().count);
  callback(Status::OK);
}

void CobaltLoggerFailsLogEvent::LogEvent(uint32_t metric_id, uint32_t event_code,
                                         LogEventCallback callback) {
  callback(Status::INVALID_ARGUMENTS);
}

void CobaltLoggerIgnoresFirstEvents::LogEvent(uint32_t metric_id, uint32_t event_code,
                                              fuchsia::cobalt::Logger::LogEventCallback callback) {
  ++num_calls_;
  if (num_calls_ <= to_ignore_) {
    return;
  }
  MarkLogEventAsCalled();
  SetLastEvent(metric_id, event_code, /*count=*/0);
  callback(Status::OK);
}

}  // namespace stubs
}  // namespace forensics
