// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testing/fake_logger.h"

#include <vector>

using fuchsia::cobalt::CobaltEvent;
using fuchsia::cobalt::CustomEventValue;
using fuchsia::cobalt::HistogramBucket;
using fuchsia::cobalt::Status;

namespace cobalt {

zx_status_t FakeLogger_Sync::LogEvent(uint32_t metric_id, uint32_t event_code, Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogEvent;
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogEventCount(uint32_t metric_id, uint32_t event_code,
                                           ::std::string component, int64_t period_duration_micros,
                                           int64_t count, Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogEventCount;
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                                            ::std::string component, int64_t elapsed_micros,
                                            Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogElapsedTime;
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  last_elapsed_time_ = elapsed_micros;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogFrameRate(uint32_t metric_id, uint32_t event_code,
                                          ::std::string component, float fps, Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogFrameRate;
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                                            ::std::string component, int64_t bytes,
                                            Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogMemoryUsage;
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::StartTimer(uint32_t metric_id, uint32_t event_code,
                                        ::std::string component, ::std::string timer_id,
                                        uint64_t timestamp, uint32_t timeout_s,
                                        Status* out_status) {
  call_count_++;
  last_metric_id_ = metric_id;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::EndTimer(::std::string timer_id, uint64_t timestamp,
                                      uint32_t timeout_s, Status* out_status) {
  call_count_++;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogIntHistogram(uint32_t metric_id, uint32_t event_code,
                                             ::std::string component,
                                             ::std::vector<HistogramBucket> histogram,
                                             Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogIntHistogram;
  last_metric_id_ = metric_id;
  last_event_code_ = event_code;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogCustomEvent(uint32_t metric_id,
                                            ::std::vector<CustomEventValue> event_values,
                                            Status* out_status) {
  last_metric_id_ = metric_id;
  *out_status = Status::OK;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogCobaltEvent(CobaltEvent event, Status* out_status) {
  call_count_++;
  *out_status = Status::OK;
  last_metric_id_ = event.metric_id;
  last_event_code_ = event.event_codes.size() > 0 ? event.event_codes[0] : -1;  // first position
  last_event_code_second_position_ = event.event_codes.size() > 1 ? event.event_codes[1] : -1;
  last_log_method_invoked_ = kLogCobaltEvent;
  return ZX_OK;
}
zx_status_t FakeLogger_Sync::LogCobaltEvents(::std::vector<CobaltEvent> events,
                                             Status* out_status) {
  call_count_++;
  last_log_method_invoked_ = kLogCobaltEvents;
  last_event_code_ =
      events.back().event_codes.size() > 0 ? events.back().event_codes[0] : -1;  // first position
  last_event_code_second_position_ =
      events.back().event_codes.size() > 1 ? events.back().event_codes[1] : -1;
  last_metric_id_ = events.back().metric_id;
  *out_status = Status::OK;
  event_count_ = events.size();
  logged_events_ = std::move(events);
  return ZX_OK;
}

}  // namespace cobalt
