// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/cobalt_event_builder.h"

#include "gtest/gtest.h"

namespace cobalt {

using fuchsia::cobalt::CobaltEvent;
using fuchsia::cobalt::CountEvent;
using fuchsia::cobalt::HistogramBucket;

const uint32_t kMetricId = 123;
const uint32_t kDimension1 = 456;
const uint32_t kDimension2 = 789;
const std::string kComponent = "Component1";

TEST(CobaltEventBuilder, CountEvent) {
  uint64_t duration = 10101;
  uint64_t count = 100;

  CobaltEvent event;
  event.metric_id = kMetricId;
  CountEvent payload;
  payload.period_duration_micros = duration;
  payload.count = count;
  event.payload.set_event_count(std::move(payload));

  ASSERT_TRUE(fidl::Equals(event, CobaltEventBuilder(kMetricId).as_count_event(duration, count)));
}

TEST(CobaltEventBuilder, ElapsedTimeEvent) {
  uint64_t elapsed_micros = 5678;

  CobaltEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.payload.set_elapsed_micros(elapsed_micros);

  ASSERT_TRUE(fidl::Equals(event, CobaltEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .as_elapsed_time(elapsed_micros)));
}

TEST(CobaltEventBuilder, FrameRateEvent) {
  float fps = 29.98;

  CobaltEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.component = kComponent;
  event.payload.set_fps(fps);

  ASSERT_TRUE(fidl::Equals(event, CobaltEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .with_component(kComponent)
                                      .as_frame_rate(fps)));
}

TEST(CobaltEventBuilder, MemoryUsage) {
  int64_t bytes_used = 13428;

  CobaltEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.component = kComponent;
  event.payload.set_memory_bytes_used(bytes_used);

  ASSERT_TRUE(fidl::Equals(event, CobaltEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .with_component(kComponent)
                                      .as_memory_usage(bytes_used)));
}

TEST(CobaltEventBuilder, IntHistogram) {
  std::vector<HistogramBucket> int_histogram;
  int_histogram.push_back({0, 10});
  int_histogram.push_back({1, 20});
  int_histogram.push_back({2, 30});
  int_histogram.push_back({3, 40});

  CobaltEvent event;
  event.metric_id = kMetricId;
  event.event_codes.push_back(kDimension1);
  event.event_codes.push_back(kDimension2);
  event.component = kComponent;
  event.payload.set_int_histogram(int_histogram);

  ASSERT_TRUE(fidl::Equals(event, CobaltEventBuilder(kMetricId)
                                      .with_event_codes({kDimension1, kDimension2})
                                      .with_component(kComponent)
                                      .as_int_histogram(int_histogram)));
}

TEST(CobaltEventBuilder, Clone) {
  uint64_t elapsed_micros = 5678;
  auto b = std::move(CobaltEventBuilder(kMetricId)
                         .with_event_codes({kDimension1, kDimension2})
                         .with_component(kComponent));

  ASSERT_FALSE(fidl::Equals(b.as_elapsed_time(elapsed_micros), b.as_elapsed_time(elapsed_micros)));

  auto b2 = std::move(CobaltEventBuilder(kMetricId)
                          .with_event_codes({kDimension1, kDimension2})
                          .with_component(kComponent));

  ASSERT_TRUE(
      fidl::Equals(b2.Clone().as_elapsed_time(elapsed_micros), b2.as_elapsed_time(elapsed_micros)));
}

TEST(CobaltEventBuilder, event_code_at) {
  uint64_t elapsed_micros = 5678;
  ASSERT_TRUE(fidl::Equals(CobaltEventBuilder(kMetricId)
                               .with_event_codes({kDimension1, kDimension2})
                               .as_elapsed_time(elapsed_micros),
                           CobaltEventBuilder(kMetricId)
                               .with_event_code_at(1, kDimension2)
                               .with_event_code_at(0, kDimension1)
                               .as_elapsed_time(elapsed_micros)));
}

TEST(CobaltEventBuilderDeathTest, event_code_at) {
  // Event code indices >= 5 are invalid.
  ASSERT_DEATH_IF_SUPPORTED(CobaltEventBuilder(kMetricId).with_event_code_at(5, 10),
                            "Invalid index");
}

}  // namespace cobalt
