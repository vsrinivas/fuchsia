// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_STUB_COBALT_LOGGER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_STUB_COBALT_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include "src/lib/fxl/logging.h"

namespace feedback {

// Defines the interface all stub loggers must implement and provides common functionality.
class StubCobaltLoggerBase : public fuchsia::cobalt::Logger {
 public:
  virtual ~StubCobaltLoggerBase() = default;

  uint32_t LastMetricId() const {
    FXL_CHECK(was_function_called_);
    return last_metric_id_;
  }

  uint32_t LastEventCode() const {
    FXL_CHECK(was_function_called_);
    return last_event_code_;
  }

  bool WasLogEventCalled() const { return WasFunctionCalled(Function::LogEvent); }
  bool WasLogEventCountCalled() const { return WasFunctionCalled(Function::LogEventCount); }
  bool WasLogElapsedTimeCalled() const { return WasFunctionCalled(Function::LogElapsedTime); }
  bool WasLogFrameRateCalled() const { return WasFunctionCalled(Function::LogFrameRate); }
  bool WasLogMemoryUsageCalled() const { return WasFunctionCalled(Function::LogMemoryUsage); }
  bool WasLogStringCalled() const { return WasFunctionCalled(Function::LogString); }
  bool WasStartTimerCalled() const { return WasFunctionCalled(Function::StartTimer); }
  bool WasEndTimerCalled() const { return WasFunctionCalled(Function::EndTimer); }
  bool WasLogIntHistogramCalled() const { return WasFunctionCalled(Function::LogIntHistogram); }
  bool WasLogCustomEventCalled() const { return WasFunctionCalled(Function::LogCustomEvent); }
  bool WasLogCobaltEventCalled() const { return WasFunctionCalled(Function::LogCobaltEvent); }
  bool WasLogCobaltEventsCalled() const { return WasFunctionCalled(Function::LogCobaltEvents); }

 protected:
  void SetLastMetricIdAndEventCode(uint32_t metric_id, uint32_t event_code) {
    last_metric_id_ = metric_id;
    last_event_code_ = event_code;
  }

  void MarkLogEventAsCalled() { MarkFunctionAsCalled(Function::LogEvent); }
  void MarkLogEventCountAsCalled() { return MarkFunctionAsCalled(Function::LogEventCount); }
  void MarkLogElapsedTimeCalled() { return MarkFunctionAsCalled(Function::LogElapsedTime); }
  void MarkLogFrameRateCalled() { return MarkFunctionAsCalled(Function::LogFrameRate); }
  void MarkLogMemoryUsageCalled() { return MarkFunctionAsCalled(Function::LogMemoryUsage); }
  void MarkLogStringCalled() { return MarkFunctionAsCalled(Function::LogString); }
  void MarkStartTimerCalled() { return MarkFunctionAsCalled(Function::StartTimer); }
  void MarkEndTimerCalled() { return MarkFunctionAsCalled(Function::EndTimer); }
  void MarkLogIntHistogramCalled() { return MarkFunctionAsCalled(Function::LogIntHistogram); }
  void MarkLogCustomEventAsCalled() { return MarkFunctionAsCalled(Function::LogCustomEvent); }
  void MarkLogCobaltEventAsCalled() { return MarkFunctionAsCalled(Function::LogCobaltEvent); }
  void MarkLogCobaltEventsAsCalled() { return MarkFunctionAsCalled(Function::LogCobaltEvents); }

  virtual void LogEvent(uint32_t metric_id, uint32_t event_code,
                        fuchsia::cobalt::Logger::LogEventCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  virtual void LogEventCount(uint32_t metric_id, uint32_t event_code, ::std::string component,
                             int64_t period_duration_micros, int64_t count,
                             fuchsia::cobalt::Logger::LogEventCountCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogElapsedTime(uint32_t metric_id, uint32_t event_code, ::std::string component,
                              int64_t elapsed_micros,
                              fuchsia::cobalt::Logger::LogElapsedTimeCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogFrameRate(uint32_t metric_id, uint32_t event_code, ::std::string component,
                            float fps,
                            fuchsia::cobalt::Logger::LogFrameRateCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogMemoryUsage(uint32_t metric_id, uint32_t event_code, ::std::string component,
                              int64_t bytes,
                              fuchsia::cobalt::Logger::LogMemoryUsageCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogString(uint32_t metric_id, ::std::string s,
                         fuchsia::cobalt::Logger::LogStringCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void StartTimer(uint32_t metric_id, uint32_t event_code, ::std::string component,
                          ::std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
                          fuchsia::cobalt::Logger::StartTimerCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void EndTimer(::std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
                        fuchsia::cobalt::Logger::EndTimerCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogIntHistogram(uint32_t metric_id, uint32_t event_code, ::std::string component,
                               ::std::vector<fuchsia::cobalt::HistogramBucket> histogram,
                               fuchsia::cobalt::Logger::LogIntHistogramCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogCustomEvent(uint32_t metric_id,
                              ::std::vector<fuchsia::cobalt::CustomEventValue> event_values,
                              fuchsia::cobalt::Logger::LogCustomEventCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogCobaltEvent(fuchsia::cobalt::CobaltEvent event,
                              fuchsia::cobalt::Logger::LogCobaltEventCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  virtual void LogCobaltEvents(::std::vector<fuchsia::cobalt::CobaltEvent> events,
                               fuchsia::cobalt::Logger::LogCobaltEventsCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

 private:
  // Each of the functions fuchsia.cobalt.Logger exposes.
  //
  // Define each element as a power of two for easy masking with |was_function_called_|.
  enum class Function {
    LogEvent = 1 << 0,
    LogEventCount = 1 << 1,
    LogElapsedTime = 1 << 2,
    LogFrameRate = 1 << 3,
    LogMemoryUsage = 1 << 4,
    LogString = 1 << 5,
    StartTimer = 1 << 6,
    EndTimer = 1 << 7,
    LogIntHistogram = 1 << 8,
    LogCustomEvent = 1 << 9,
    LogCobaltEvent = 1 << 10,
    LogCobaltEvents = 1 << 11,
  };

  // Record a function as having been called.
  inline void MarkFunctionAsCalled(Function f) { was_function_called_ |= static_cast<uint32_t>(f); }

  // Determine if a function was called.
  inline bool WasFunctionCalled(Function f) const {
    return was_function_called_ & static_cast<uint32_t>(f);
  }

  uint32_t last_metric_id_ = 0;
  uint32_t last_event_code_ = 0;

  // Store whether or not a function was called in a bit field.
  uint32_t was_function_called_ = 0;
};

// Always record |metric_id| and |event_code| and call callback with |Status::OK|.
class StubCobaltLogger : public StubCobaltLoggerBase {
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                fuchsia::cobalt::Logger::LogEventCallback callback) override;
};

// Fail to acknowledge that LogEvent() was called and return |Status::INVALID_ARGUMENTS|.
class StubCobaltLoggerFailsLogEvent : public StubCobaltLoggerBase {
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                fuchsia::cobalt::Logger::LogEventCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_STUB_COBALT_LOGGER_FACTORY_H_
