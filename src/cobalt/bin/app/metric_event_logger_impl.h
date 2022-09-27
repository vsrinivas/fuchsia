// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_METRIC_EVENT_LOGGER_IMPL_H_
#define SRC_COBALT_BIN_APP_METRIC_EVENT_LOGGER_IMPL_H_

#include <fuchsia/metrics/cpp/fidl.h>

#include "third_party/cobalt/src/logger/logger.h"

namespace cobalt {

// Implementations of the MetricEventLogger fidl interfaces.
//
// To test run:
//    fx set --with-base //bundles:tools,//src/cobalt/bin:cobalt_tests;
//    fx test cobalt_testapp_no_network
class MetricEventLoggerImpl : public fuchsia::metrics::MetricEventLogger {
 public:
  MetricEventLoggerImpl(std::unique_ptr<logger::LoggerInterface> logger);

 private:
  void LogOccurrence(uint32_t metric_id, uint64_t count, ::std::vector<uint32_t> event_codes,
                     fuchsia::metrics::MetricEventLogger::LogOccurrenceCallback callback) override;

  void LogInteger(uint32_t metric_id, int64_t value, ::std::vector<uint32_t> event_codes,
                  fuchsia::metrics::MetricEventLogger::LogIntegerCallback callback) override;

  void LogIntegerHistogram(
      uint32_t metric_id, std::vector<fuchsia::metrics::HistogramBucket> histogram,
      ::std::vector<uint32_t> event_codes,
      fuchsia::metrics::MetricEventLogger::LogIntegerHistogramCallback callback) override;

  void LogString(uint32_t metric_id, std::string string_value, ::std::vector<uint32_t> event_codes,
                 fuchsia::metrics::MetricEventLogger::LogStringCallback callback) override;

  void LogMetricEvents(
      std::vector<fuchsia::metrics::MetricEvent> events,
      fuchsia::metrics::MetricEventLogger::LogMetricEventsCallback callback) override;

 private:
  fpromise::result<void, fuchsia::metrics::Error> LogMetricEvent(
      fuchsia::metrics::MetricEvent event);

  std::unique_ptr<logger::LoggerInterface> logger_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_METRIC_EVENT_LOGGER_IMPL_H_
