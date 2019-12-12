// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/testing/mock_cobalt_logger.h"

namespace cobalt {

void MockCobaltLogger::LogEvent(uint32_t metric_id, uint32_t event_code) {
  (*call_counts_)[LogMethod::kLogEvent]++;
}
void MockCobaltLogger::LogEventCount(uint32_t metric_id, uint32_t event_code,
                                     const std::string& component, zx::duration period_duration,
                                     int64_t count) {
  (*call_counts_)[LogMethod::kLogEventCount]++;
}
void MockCobaltLogger::LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                                      const std::string& component, zx::duration elapsed_time) {
  (*call_counts_)[LogMethod::kLogElapsedTime]++;
}
void MockCobaltLogger::LogFrameRate(uint32_t metric_id, uint32_t event_code,
                                    const std::string& component, float fps) {
  (*call_counts_)[LogMethod::kLogFrameRate]++;
}
void MockCobaltLogger::LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                                      const std::string& component, int64_t bytes) {
  (*call_counts_)[LogMethod::kLogMemoryUsage]++;
}
void MockCobaltLogger::LogIntHistogram(uint32_t metric_id, uint32_t event_code,
                                       const std::string& component,
                                       std::vector<fuchsia::cobalt::HistogramBucket> histogram) {
  (*call_counts_)[LogMethod::kLogIntHistogram]++;
}
void MockCobaltLogger::LogCustomEvent(uint32_t metric_id,
                                      std::vector<fuchsia::cobalt::CustomEventValue> event_values) {
  (*call_counts_)[LogMethod::kLogCustomEvent]++;
}
void MockCobaltLogger::LogCobaltEvent(fuchsia::cobalt::CobaltEvent event) {
  (*call_counts_)[LogMethod::kLogCobaltEvent]++;
}
void MockCobaltLogger::LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events) {
  (*call_counts_)[LogMethod::kLogCobaltEvents]++;
}

}  // namespace cobalt
