// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/metric_event_logger_impl.h"

#include <lib/trace/event.h>

#include "src/cobalt/bin/app/utils.h"

namespace cobalt {

MetricEventLoggerImpl::MetricEventLoggerImpl(std::unique_ptr<logger::LoggerInterface> logger)
    : logger_(std::move(logger)) {}

void MetricEventLoggerImpl::LogOccurrence(
    uint32_t metric_id, uint64_t count, ::std::vector<uint32_t> event_codes,
    fuchsia::metrics::MetricEventLogger::LogOccurrenceCallback callback) {
  TRACE_DURATION("cobalt_fidl", "MetricEventLoggerImpl::LogOccurrence");
  callback(ToMetricsResult(logger_->LogOccurrence(metric_id, count, event_codes)));
}

void MetricEventLoggerImpl::LogInteger(
    uint32_t metric_id, int64_t value, ::std::vector<uint32_t> event_codes,
    fuchsia::metrics::MetricEventLogger::LogIntegerCallback callback) {
  TRACE_DURATION("cobalt_fidl", "MetricEventLoggerImpl::LogInteger");
  callback(ToMetricsResult(logger_->LogInteger(metric_id, value, event_codes)));
}

void MetricEventLoggerImpl::LogIntegerHistogram(
    uint32_t metric_id, std::vector<fuchsia::metrics::HistogramBucket> histogram,
    ::std::vector<uint32_t> event_codes,
    fuchsia::metrics::MetricEventLogger::LogIntegerHistogramCallback callback) {
  TRACE_DURATION("cobalt_fidl", "MetricEventLoggerImpl::LogIntegerHistogram");
  logger::HistogramPtr histogram_ptr(new google::protobuf::RepeatedPtrField<HistogramBucket>());
  for (auto it = histogram.begin(); histogram.end() != it; it++) {
    auto bucket = histogram_ptr->Add();
    bucket->set_index((*it).index);
    bucket->set_count((*it).count);
  }
  callback(ToMetricsResult(
      logger_->LogIntegerHistogram(metric_id, std::move(histogram_ptr), event_codes)));
}

void MetricEventLoggerImpl::LogString(
    uint32_t metric_id, std::string string_value, ::std::vector<uint32_t> event_codes,
    fuchsia::metrics::MetricEventLogger::LogStringCallback callback) {
  TRACE_DURATION("cobalt_fidl", "MetricEventLoggerImpl::LogString");
  callback(ToMetricsResult(logger_->LogString(metric_id, string_value, event_codes)));
}

void MetricEventLoggerImpl::LogMetricEvents(
    std::vector<fuchsia::metrics::MetricEvent> events,
    fuchsia::metrics::MetricEventLogger::LogMetricEventsCallback callback) {
  TRACE_DURATION("cobalt_fidl", "MetricEventLoggerImpl::LogMetricEvents");
  logger_->RecordLoggerCall(
      logger::LoggerCallsMadeMigratedMetricDimensionLoggerMethod::LogMetricEvents);

  // tracking LoggerCalled events is expensive (~3.5ms/event). We want
  // LogCobaltEvents to be a more performance concious alternative, so we pause
  // this logging while we work through the batch.
  logger_->PauseInternalLogging();

  auto failures = 0;

  auto end = std::make_move_iterator(events.end());

  for (auto it = std::make_move_iterator(events.begin()); it != end; it++) {
    if (LogMetricEvent(std::move(*it)).is_error()) {
      failures += 1;
    }
  }

  logger_->ResumeInternalLogging();

  if (failures == 0) {
    callback(fpromise::ok());
  } else {
    callback(fpromise::error(fuchsia::metrics::Error::INTERNAL_ERROR));
  }
}

using fuchsia::metrics::MetricEventPayload;
fpromise::result<void, fuchsia::metrics::Error> MetricEventLoggerImpl::LogMetricEvent(
    fuchsia::metrics::MetricEvent event) {
  TRACE_DURATION("cobalt_fidl", "MetricEventLoggerImpl::LogMetricEvent");

  switch (event.payload.Which()) {
    case MetricEventPayload::Tag::kCount:
      return ToMetricsResult(
          logger_->LogOccurrence(event.metric_id, event.payload.count(), event.event_codes));

    case MetricEventPayload::Tag::kIntegerValue:
      return ToMetricsResult(
          logger_->LogInteger(event.metric_id, event.payload.integer_value(), event.event_codes));

    case MetricEventPayload::Tag::kHistogram: {
      auto histogram = std::move(event.payload.histogram());
      logger::HistogramPtr histogram_ptr(new google::protobuf::RepeatedPtrField<HistogramBucket>());
      for (auto it = histogram.begin(); histogram.end() != it; it++) {
        auto bucket = histogram_ptr->Add();
        bucket->set_index((*it).index);
        bucket->set_count((*it).count);
      }
      return ToMetricsResult(logger_->LogIntegerHistogram(event.metric_id, std::move(histogram_ptr),
                                                          event.event_codes));
    }

    case MetricEventPayload::Tag::kStringValue:
      return ToMetricsResult(
          logger_->LogString(event.metric_id, event.payload.string_value(), event.event_codes));

    default:
      return fpromise::error(fuchsia::metrics::Error::INVALID_ARGUMENTS);
  }
}

}  // namespace cobalt
