// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_COBALT_CPP_TESTING_MOCK_COBALT_LOGGER_H_
#define SRC_LIB_COBALT_CPP_TESTING_MOCK_COBALT_LOGGER_H_

#include <unordered_map>

#include "src/cobalt/bin/testing/log_method.h"
#include "src/lib/cobalt/cpp/cobalt_logger.h"

namespace cobalt {

using CallCountMap = std::unordered_map<LogMethod, uint32_t>;

class MockCobaltLogger : public cobalt::CobaltLogger {
 public:
  MockCobaltLogger(CallCountMap* call_counts = nullptr) : call_counts_(call_counts) {}
  ~MockCobaltLogger() override = default;
  void LogIntHistogram(uint32_t metric_id, uint32_t event_code, const std::string& component,
                       std::vector<fuchsia::cobalt::HistogramBucket> histogram) override;
  void LogEvent(uint32_t metric_id, uint32_t event_code) override;
  void LogEventCount(uint32_t metric_id, uint32_t event_code, const std::string& component,
                     zx::duration period_duration, int64_t count) override;
  void LogElapsedTime(uint32_t metric_id, uint32_t event_code, const std::string& component,
                      zx::duration elapsed_time) override;
  void LogFrameRate(uint32_t metric_id, uint32_t event_code, const std::string& component,
                    float fps) override;
  void LogMemoryUsage(uint32_t metric_id, uint32_t event_code, const std::string& component,
                      int64_t bytes) override;
  void StartTimer(uint32_t metric_id, uint32_t event_code, const std::string& component,
                  const std::string& timer_id, zx::time timestamp, zx::duration timeout) override{};
  void EndTimer(const std::string& timer_id, zx::time timestamp, zx::duration timeout) override{};
  void LogCustomEvent(uint32_t metric_id,
                      std::vector<fuchsia::cobalt::CustomEventValue> event_values) override;
  void LogCobaltEvent(fuchsia::cobalt::CobaltEvent event) override;
  void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events) override;

 private:
  CallCountMap* call_counts_ = nullptr;
};

}  // namespace cobalt

#endif  // SRC_LIB_COBALT_CPP_TESTING_MOCK_COBALT_LOGGER_H_
