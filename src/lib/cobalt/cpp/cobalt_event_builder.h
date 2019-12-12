// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_COBALT_CPP_COBALT_EVENT_BUILDER_H_
#define SRC_LIB_COBALT_CPP_COBALT_EVENT_BUILDER_H_

#include <fuchsia/cobalt/cpp/fidl.h>

namespace cobalt {

// CobaltEventBuilder is a tool to make it easier and less error-prone to
// construct CobaltEvent objects.
//
// Without this API you would log a cobalt event like this:
//
//     CobaltEvent event;
//     event.metric_id = metric_id;
//     event.event_codes.push_back(dimension_one);
//     event.event_codes.push_back(dimension_two);
//     event.component = component;
//
//     CountEvent payload;
//     payload.period_duration_micros = period_duration_micros;
//     payload.count = count;
//     event.payload.set_event_count(std::move(payload));
//
//     logger_->LogCobaltEvent(std::move(event), &status);
//
// With the API it can be a lot cleaner:
//
//     logger_->LogCobaltEvent(
//         CobaltEventBuilder(metric_id)
//             .with_event_codes({dimension_one, dimension_two})
//             .with_component(std::move(component))
//             .as_count_event(period_duration_micros, count),
//         &status);

class CobaltEventBuilder {
 private:
  CobaltEventBuilder() {}

 public:
  explicit CobaltEventBuilder(uint32_t metric_id);

  CobaltEventBuilder &with_event_code(const uint32_t event_code);
  CobaltEventBuilder &with_event_codes(std::vector<uint32_t> event_codes);

  // Panics if index >= 5
  CobaltEventBuilder &with_event_code_at(const uint32_t index, const uint32_t event_code);
  CobaltEventBuilder &with_component(std::string component);
  CobaltEventBuilder Clone() const;

  fuchsia::cobalt::CobaltEvent as_count_event(const int64_t period_duration_micros,
                                              const int64_t count);
  fuchsia::cobalt::CobaltEvent as_elapsed_time(const int64_t elapsed_micros);
  fuchsia::cobalt::CobaltEvent as_frame_rate(const float fps);
  fuchsia::cobalt::CobaltEvent as_memory_usage(const int64_t memory_bytes_used);
  fuchsia::cobalt::CobaltEvent as_int_histogram(
      std::vector<fuchsia::cobalt::HistogramBucket> int_histogram);
  fuchsia::cobalt::CobaltEvent as_event();

 private:
  fuchsia::cobalt::CobaltEvent event_;
};

}  // namespace cobalt

#endif  // SRC_LIB_COBALT_CPP_COBALT_EVENT_BUILDER_H_
