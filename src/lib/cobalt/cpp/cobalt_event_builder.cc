// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/cobalt_event_builder.h"

#include "src/lib/fxl/logging.h"

namespace cobalt {

using fuchsia::cobalt::CobaltEvent;
using fuchsia::cobalt::CountEvent;
using fuchsia::cobalt::Event;
using fuchsia::cobalt::HistogramBucket;

CobaltEventBuilder::CobaltEventBuilder(uint32_t metric_id) { event_.metric_id = metric_id; }

CobaltEventBuilder &CobaltEventBuilder::with_event_code(const uint32_t event_code) {
  event_.event_codes.push_back(event_code);
  return *this;
}

CobaltEventBuilder &CobaltEventBuilder::with_event_code_at(const uint32_t index,
                                                           const uint32_t event_code) {
  FXL_CHECK(index < 5) << "Invalid index passed to CobaltEventBuilder::with_event_code. Cobalt "
                          "events cannot support more than 5 event_codes.";
  while (event_.event_codes.size() <= index) {
    event_.event_codes.push_back(0);
  }
  event_.event_codes[index] = event_code;
  return *this;
}

CobaltEventBuilder &CobaltEventBuilder::with_event_codes(std::vector<uint32_t> event_codes) {
  event_.event_codes = std::move(event_codes);
  return *this;
}

CobaltEventBuilder &CobaltEventBuilder::with_component(std::string component) {
  event_.component = std::move(component);
  return *this;
}

CobaltEventBuilder CobaltEventBuilder::Clone() const {
  CobaltEventBuilder builder;
  event_.Clone(&builder.event_);
  return builder;
}

CobaltEvent CobaltEventBuilder::as_count_event(const int64_t period_duration_micros,
                                               const int64_t count) {
  CountEvent payload;
  payload.period_duration_micros = period_duration_micros;
  payload.count = count;

  event_.payload.set_event_count(std::move(payload));

  return std::move(event_);
}

CobaltEvent CobaltEventBuilder::as_elapsed_time(const int64_t elapsed_micros) {
  event_.payload.set_elapsed_micros(elapsed_micros);

  return std::move(event_);
}

CobaltEvent CobaltEventBuilder::as_frame_rate(const float fps) {
  event_.payload.set_fps(fps);

  return std::move(event_);
}

CobaltEvent CobaltEventBuilder::as_memory_usage(const int64_t memory_bytes_used) {
  event_.payload.set_memory_bytes_used(memory_bytes_used);

  return std::move(event_);
}

CobaltEvent CobaltEventBuilder::as_int_histogram(std::vector<HistogramBucket> int_histogram) {
  event_.payload.set_int_histogram(std::move(int_histogram));

  return std::move(event_);
}

CobaltEvent CobaltEventBuilder::as_event() {
  event_.payload.set_event(Event());

  return std::move(event_);
}

}  // namespace cobalt
