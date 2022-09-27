// Copyright 2022  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testing/stub_metric_event_logger.h"

#include <vector>

#include "fuchsia/metrics/cpp/fidl.h"
#include "lib/fpromise/result.h"
#include "src/cobalt/bin/testing/log_metric_method.h"

namespace cobalt {

zx_status_t StubMetricEventLogger_Sync::LogOccurrence(
    uint32_t metric_id, uint64_t count, ::std::vector<uint32_t> event_codes,
    ::fuchsia::metrics::MetricEventLogger_LogOccurrence_Result* out_result) {
  call_count_++;
  last_log_metric_method_invoked_ = LogMetricMethod::kLogOccurrence;
  last_metric_id_ = metric_id;
  last_event_codes_ = event_codes;
  *out_result = fpromise::ok();
  return ZX_OK;
}

zx_status_t StubMetricEventLogger_Sync::LogInteger(
    uint32_t metric_id, int64_t value, ::std::vector<uint32_t> event_codes,
    ::fuchsia::metrics::MetricEventLogger_LogInteger_Result* out_result) {
  call_count_++;
  last_log_metric_method_invoked_ = LogMetricMethod::kLogInteger;
  last_metric_id_ = metric_id;
  last_integer_ = value;
  last_event_codes_ = event_codes;
  *out_result = fpromise::ok();
  return ZX_OK;
}

zx_status_t StubMetricEventLogger_Sync::LogIntegerHistogram(
    uint32_t metric_id, ::std::vector<::fuchsia::metrics::HistogramBucket> histogram,
    ::std::vector<uint32_t> event_codes,
    ::fuchsia::metrics::MetricEventLogger_LogIntegerHistogram_Result* out_result) {
  call_count_++;
  last_log_metric_method_invoked_ = LogMetricMethod::kLogIntegerHistogram;
  last_metric_id_ = metric_id;
  last_event_codes_ = event_codes;
  *out_result = fpromise::ok();
  return ZX_OK;
}

zx_status_t StubMetricEventLogger_Sync::LogString(
    uint32_t metric_id, ::std::string string_value, ::std::vector<uint32_t> event_codes,
    ::fuchsia::metrics::MetricEventLogger_LogString_Result* out_result) {
  call_count_++;
  last_log_metric_method_invoked_ = LogMetricMethod::kLogString;
  last_metric_id_ = metric_id;
  last_event_codes_ = event_codes;
  *out_result = fpromise::ok();
  return ZX_OK;
}

zx_status_t StubMetricEventLogger_Sync::LogMetricEvents(
    ::std::vector<::fuchsia::metrics::MetricEvent> events,
    ::fuchsia::metrics::MetricEventLogger_LogMetricEvents_Result* out_result) {
  call_count_++;
  last_log_metric_method_invoked_ = LogMetricMethod::kLogMetricEvents;
  last_event_codes_ = events.back().event_codes;
  last_metric_id_ = events.back().metric_id;
  *out_result = fpromise::ok();
  event_count_ = events.size();
  logged_events_ = std::move(events);
  return ZX_OK;
}

}  // namespace cobalt
