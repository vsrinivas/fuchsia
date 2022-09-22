// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_LOGGER_IMPL_H_
#define SRC_COBALT_BIN_APP_LOGGER_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include "src/cobalt/bin/app/timer_manager.h"
#include "third_party/cobalt/src/logger/logger.h"

namespace cobalt {

// Implementations of the Logger fidl interface.
//
// To test run:
//    fx set --with-base //bundles:tools,//src/cobalt/bin:cobalt_tests;
//    fx test cobalt_testapp_no_network
class LoggerImpl : public fuchsia::cobalt::Logger {
 public:
  LoggerImpl(std::unique_ptr<logger::LoggerInterface> logger, TimerManager* timer_manager);

 private:
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                fuchsia::cobalt::LoggerBase::LogEventCallback callback) override;

  void LogEventCount(uint32_t metric_id, uint32_t event_code, std::string component,
                     int64_t period_duration_micros, int64_t count,
                     fuchsia::cobalt::LoggerBase::LogEventCountCallback callback) override;

  void LogElapsedTime(uint32_t metric_id, uint32_t event_code, std::string component,
                      int64_t elapsed_micros,
                      fuchsia::cobalt::LoggerBase::LogElapsedTimeCallback callback) override;

  void LogFrameRate(uint32_t metric_id, uint32_t event_code, std::string component, float fps,
                    fuchsia::cobalt::LoggerBase::LogFrameRateCallback callback) override;

  void LogMemoryUsage(uint32_t metric_id, uint32_t event_code, std::string component, int64_t bytes,
                      fuchsia::cobalt::LoggerBase::LogMemoryUsageCallback callback) override;

  void LogIntHistogram(uint32_t metric_id, uint32_t event_code, std::string component,
                       std::vector<fuchsia::cobalt::HistogramBucket> histogram,
                       fuchsia::cobalt::Logger::LogIntHistogramCallback callback) override;

  void LogCustomEvent(uint32_t metric_id,
                      std::vector<fuchsia::cobalt::CustomEventValue> event_values,
                      fuchsia::cobalt::Logger::LogCustomEventCallback callback) override;

  template <class CB>
  void AddTimerObservationIfReady(std::unique_ptr<TimerVal> timer_val_ptr, CB callback);

  void StartTimer(uint32_t metric_id, uint32_t event_code, std::string component,
                  std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
                  fuchsia::cobalt::LoggerBase::StartTimerCallback callback) override;

  void EndTimer(std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
                fuchsia::cobalt::LoggerBase::EndTimerCallback callback) override;

  void LogCobaltEvent(fuchsia::cobalt::CobaltEvent event,
                      fuchsia::cobalt::Logger::LogCobaltEventCallback callback) override;

  void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events,
                       fuchsia::cobalt::Logger::LogCobaltEventCallback callback) override;

 private:
  std::unique_ptr<logger::LoggerInterface> logger_;
  TimerManager* timer_manager_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_LOGGER_IMPL_H_
