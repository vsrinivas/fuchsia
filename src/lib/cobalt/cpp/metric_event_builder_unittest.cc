// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/metric_event_builder.h"

#include "gtest/gtest.h"

namespace cobalt {

using fuchsia::cobalt::MetricEvent;

const uint32_t kMetricId = 123;
const uint32_t kDimension1 = 456;
const uint32_t kDimension2 = 789;

TEST(MetricEventBuilder, OccurrenceEvent) {
  uint64_t count = 100;

  MetricEvent event;
  event.metric_id = kMetricId;
  event.payload.set_count(count);

  ASSERT_TRUE(fidl::Equals(event, MetricEventBuilder(kMetricId).as_occurrence(count)));
}

TEST(MetricEventBuilder, IntegerEvent) {
  int64_t integer_value = 5678;

  MetricEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.payload.set_integer_value(integer_value);

  ASSERT_TRUE(fidl::Equals(event, MetricEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .as_integer(integer_value)));
}

TEST(MetricEventBuilder, IntegerHistogram) {
  std::vector<fuchsia::cobalt::HistogramBucket> histogram;
  histogram.push_back({0, 10});
  histogram.push_back({1, 20});
  histogram.push_back({2, 30});
  histogram.push_back({3, 40});

  MetricEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.payload.set_histogram(histogram);

  ASSERT_TRUE(fidl::Equals(event, MetricEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .as_integer_histogram(histogram)));
}

TEST(MetricEventBuilder, StringEvent) {
  std::string string_value = "test-string";

  MetricEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.payload.set_string_value(string_value);

  ASSERT_TRUE(fidl::Equals(event, MetricEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .as_string(string_value)));
}

TEST(MetricEventBuilder, Clone) {
  uint64_t integer_value = 5678;
  auto b = std::move(MetricEventBuilder(kMetricId).with_event_codes({kDimension1, kDimension2}));

  ASSERT_FALSE(fidl::Equals(b.as_integer(integer_value), b.as_integer(integer_value)));

  auto b2 = std::move(MetricEventBuilder(kMetricId).with_event_codes({kDimension1, kDimension2}));

  ASSERT_TRUE(fidl::Equals(b2.Clone().as_integer(integer_value), b2.as_integer(integer_value)));
}

TEST(MetricEventBuilder, event_code_at) {
  uint64_t integer_value = 5678;
  ASSERT_TRUE(fidl::Equals(MetricEventBuilder(kMetricId)
                               .with_event_codes({kDimension1, kDimension2})
                               .as_integer(integer_value),
                           MetricEventBuilder(kMetricId)
                               .with_event_code_at(1, kDimension2)
                               .with_event_code_at(0, kDimension1)
                               .as_integer(integer_value)));
}

}  // namespace cobalt
