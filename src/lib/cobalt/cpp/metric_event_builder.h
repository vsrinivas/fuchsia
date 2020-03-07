// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_COBALT_CPP_METRIC_EVENT_BUILDER_H_
#define SRC_LIB_COBALT_CPP_METRIC_EVENT_BUILDER_H_

#include <fuchsia/cobalt/cpp/fidl.h>

namespace cobalt {

// MetricEventBuilder is a tool to make it easier and less error-prone to
// construct MetricEvent objects.
//
// Without this API you would log a cobalt event like this:
//
//     MetricEvent event;
//     event.metric_id = metric_id;
//     event.event_codes.push_back(dimension_one);
//     event.event_codes.push_back(dimension_two);
//     event.payload.set_count(count);
//     logger_->LogMetricEvent(std::move(event), &status);
//
// With the API it can be a lot cleaner:
//
//     logger_->LogMetricEvent(
//         MetricEventBuilder(metric_id)
//             .with_event_codes({dimension_one, dimension_two})
//             .as_occurrence(count),
//         &status);
//
class MetricEventBuilder {
 public:
  explicit MetricEventBuilder(uint32_t metric_id);

  MetricEventBuilder &with_event_code(uint32_t event_code);
  MetricEventBuilder &with_event_codes(std::vector<uint32_t> event_codes);
  MetricEventBuilder &with_event_code_at(uint32_t index, uint32_t event_code);

  [[nodiscard]] MetricEventBuilder Clone() const;

  fuchsia::cobalt::MetricEvent as_occurrence(int64_t count);
  fuchsia::cobalt::MetricEvent as_integer(int64_t integer_value);
  fuchsia::cobalt::MetricEvent as_integer_histogram(
      std::vector<fuchsia::cobalt::HistogramBucket> histogram);
  fuchsia::cobalt::MetricEvent as_string(const std::string &string_value);

 private:
  MetricEventBuilder() = default;

  fuchsia::cobalt::MetricEvent event_;
};

}  // namespace cobalt

#endif  // SRC_LIB_COBALT_CPP_METRIC_EVENT_BUILDER_H_
