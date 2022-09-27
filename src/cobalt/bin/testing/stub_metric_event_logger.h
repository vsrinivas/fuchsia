// Copyright 2022  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTING_STUB_METRIC_EVENT_LOGGER_H_
#define SRC_COBALT_BIN_TESTING_STUB_METRIC_EVENT_LOGGER_H_

#include <fuchsia/metrics/cpp/fidl.h>

#include "src/cobalt/bin/testing/log_metric_method.h"

namespace cobalt {

class StubMetricEventLogger_Sync : public fuchsia::metrics::MetricEventLogger_Sync {
 public:
  zx_status_t LogOccurrence(
      uint32_t metric_id, uint64_t count, ::std::vector<uint32_t> event_codes,
      ::fuchsia::metrics::MetricEventLogger_LogOccurrence_Result* out_result) override;
  zx_status_t LogInteger(
      uint32_t metric_id, int64_t value, ::std::vector<uint32_t> event_codes,
      ::fuchsia::metrics::MetricEventLogger_LogInteger_Result* out_result) override;
  zx_status_t LogIntegerHistogram(
      uint32_t metric_id, ::std::vector<::fuchsia::metrics::HistogramBucket> histogram,
      ::std::vector<uint32_t> event_codes,
      ::fuchsia::metrics::MetricEventLogger_LogIntegerHistogram_Result* out_result) override;
  zx_status_t LogString(
      uint32_t metric_id, ::std::string string_value, ::std::vector<uint32_t> event_codes,
      ::fuchsia::metrics::MetricEventLogger_LogString_Result* out_result) override;
  zx_status_t LogMetricEvents(
      ::std::vector<::fuchsia::metrics::MetricEvent> events,
      ::fuchsia::metrics::MetricEventLogger_LogMetricEvents_Result* out_result) override;

  uint32_t last_metric_id() { return last_metric_id_; }

  void reset_last_metric_id() { last_metric_id_ = -1; }

  std::vector<uint32_t> last_event_codes() { return last_event_codes_; }

  void reset_last_event_codes() { last_event_codes_ = {}; }

  int64_t last_integer() { return last_integer_; }

  void reset_last_integer() { last_integer_ = -1; }

  LogMetricMethod last_log_method_invoked() { return last_log_metric_method_invoked_; }

  void reset_last_log_method_invoked() {
    last_log_metric_method_invoked_ = LogMetricMethod::kDefault;
  }

  size_t call_count() { return call_count_; }

  void reset_call_count() { call_count_ = 0; }

  // Used for LogMetricEvents() only.

  size_t event_count() { return event_count_; }

  void reset_event_count() { event_count_ = 0; }

  const std::vector<fuchsia::metrics::MetricEvent>& logged_events() { return logged_events_; }

  void reset_logged_events() { logged_events_.clear(); }

  void reset() {
    reset_last_metric_id();
    reset_last_event_codes();
    reset_last_integer();
    reset_last_log_method_invoked();
    reset_call_count();
    reset_event_count();
    reset_logged_events();
  }

 private:
  uint32_t last_metric_id_ = -1;
  std::vector<uint32_t> last_event_codes_ = {};
  int64_t last_integer_ = -1;
  LogMetricMethod last_log_metric_method_invoked_ = LogMetricMethod::kDefault;
  size_t call_count_ = 0;

  // Used for LogMetricEvents() only.
  size_t event_count_ = 0;
  std::vector<fuchsia::metrics::MetricEvent> logged_events_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTING_STUB_METRIC_EVENT_LOGGER_H_
