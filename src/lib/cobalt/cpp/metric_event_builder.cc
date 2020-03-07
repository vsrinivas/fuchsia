// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/metric_event_builder.h"

#include "src/lib/fxl/logging.h"

namespace cobalt {

using fuchsia::cobalt::MetricEvent;

MetricEventBuilder::MetricEventBuilder(uint32_t metric_id) { event_.metric_id = metric_id; }

MetricEventBuilder &MetricEventBuilder::with_event_code(const uint32_t event_code) {
  event_.event_codes.push_back(event_code);
  return *this;
}

MetricEventBuilder &MetricEventBuilder::with_event_code_at(const uint32_t index,
                                                           const uint32_t event_code) {
  while (event_.event_codes.size() <= index) {
    event_.event_codes.push_back(0);
  }
  event_.event_codes[index] = event_code;
  return *this;
}

MetricEventBuilder &MetricEventBuilder::with_event_codes(std::vector<uint32_t> event_codes) {
  event_.event_codes = std::move(event_codes);
  return *this;
}

MetricEventBuilder MetricEventBuilder::Clone() const {
  MetricEventBuilder builder;
  event_.Clone(&builder.event_);
  return builder;
}

MetricEvent MetricEventBuilder::as_occurrence(const int64_t count) {
  event_.payload.set_count(count);

  return std::move(event_);
}

MetricEvent MetricEventBuilder::as_integer(const int64_t integer_value) {
  event_.payload.set_integer_value(integer_value);

  return std::move(event_);
}

MetricEvent MetricEventBuilder::as_integer_histogram(
    std::vector<fuchsia::cobalt::HistogramBucket> histogram) {
  event_.payload.set_histogram(std::move(histogram));

  return std::move(event_);
}

MetricEvent MetricEventBuilder::as_string(const std::string &string_value) {
  event_.payload.set_string_value(string_value);

  return std::move(event_);
}

}  // namespace cobalt
