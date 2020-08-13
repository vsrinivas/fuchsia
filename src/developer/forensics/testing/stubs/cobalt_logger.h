// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl_test_base.h>

#include <utility>

#include "src/developer/forensics/testing/stubs/fidl_server.h"
#include "src/developer/forensics/utils/cobalt/event.h"

namespace forensics {
namespace stubs {

// Defines the interface all stub loggers must implement and provides common functionality.
class CobaltLoggerBase : public SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::cobalt, Logger) {
 public:
  virtual ~CobaltLoggerBase() = default;

  const cobalt::Event& LastEvent() const { return events_.back(); }
  const std::vector<cobalt::Event>& Events() const { return events_; }

  bool WasLogEventCalled() const { return WasFunctionCalled(Function::LogEvent); }
  bool WasLogEventCountCalled() const { return WasFunctionCalled(Function::LogEventCount); }
  bool WasLogElapsedTimeCalled() const { return WasFunctionCalled(Function::LogElapsedTime); }
  bool WasLogFrameRateCalled() const { return WasFunctionCalled(Function::LogFrameRate); }
  bool WasLogMemoryUsageCalled() const { return WasFunctionCalled(Function::LogMemoryUsage); }
  bool WasStartTimerCalled() const { return WasFunctionCalled(Function::StartTimer); }
  bool WasEndTimerCalled() const { return WasFunctionCalled(Function::EndTimer); }
  bool WasLogIntHistogramCalled() const { return WasFunctionCalled(Function::LogIntHistogram); }
  bool WasLogCustomEventCalled() const { return WasFunctionCalled(Function::LogCustomEvent); }
  bool WasLogCobaltEventCalled() const { return WasFunctionCalled(Function::LogCobaltEvent); }
  bool WasLogCobaltEventsCalled() const { return WasFunctionCalled(Function::LogCobaltEvents); }

 protected:
  void SetLastEvent(uint32_t metric_id, uint32_t event_code, uint64_t count);
  void SetLastEvent(uint32_t metric_id, std::vector<uint32_t> event_codes, uint64_t count);

  void MarkLogEventAsCalled() { MarkFunctionAsCalled(Function::LogEvent); }
  void MarkLogEventCountAsCalled() { return MarkFunctionAsCalled(Function::LogEventCount); }
  void MarkLogElapsedTimeAsCalled() { return MarkFunctionAsCalled(Function::LogElapsedTime); }
  void MarkLogFrameRateAsCalled() { return MarkFunctionAsCalled(Function::LogFrameRate); }
  void MarkLogMemoryUsageAsCalled() { return MarkFunctionAsCalled(Function::LogMemoryUsage); }
  void MarkStartTimerAsCalled() { return MarkFunctionAsCalled(Function::StartTimer); }
  void MarkEndTimerAsCalled() { return MarkFunctionAsCalled(Function::EndTimer); }
  void MarkLogIntHistogramAsCalled() { return MarkFunctionAsCalled(Function::LogIntHistogram); }
  void MarkLogCustomEventAsCalled() { return MarkFunctionAsCalled(Function::LogCustomEvent); }
  void MarkLogCobaltEventAsCalled() { return MarkFunctionAsCalled(Function::LogCobaltEvent); }
  void MarkLogCobaltEventsAsCalled() { return MarkFunctionAsCalled(Function::LogCobaltEvents); }

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
    StartTimer = 1 << 6,
    EndTimer = 1 << 7,
    LogIntHistogram = 1 << 8,
    LogCustomEvent = 1 << 9,
    LogCobaltEvent = 1 << 10,
    LogCobaltEvents = 1 << 11,
  };

  // Record a function as having been called.
  void MarkFunctionAsCalled(Function f) { was_function_called_ |= static_cast<uint32_t>(f); }

  // Determine if a function was called.
  bool WasFunctionCalled(Function f) const {
    return was_function_called_ & static_cast<uint32_t>(f);
  }

  std::vector<cobalt::Event> events_;

  // Store whether or not a function was called in a bit field.
  uint32_t was_function_called_ = 0;
};

// Always record |metric_id| and |event_code| and call callback with |Status::OK|.
class CobaltLogger : public CobaltLoggerBase {
 public:
  // |fuchsia::cobalt::Logger|
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                fuchsia::cobalt::Logger::LogEventCallback callback) override;
  void LogEventCount(uint32_t metric_id, uint32_t event_code, ::std::string component,
                     int64_t period_duration_micros, int64_t count,
                     fuchsia::cobalt::Logger::LogEventCountCallback callback) override;
  void LogElapsedTime(uint32_t metric_id, uint32_t event_code, ::std::string component,
                      int64_t elapsed_micros,
                      fuchsia::cobalt::Logger::LogEventCountCallback callback) override;
  void LogCobaltEvent(fuchsia::cobalt::CobaltEvent event,
                      fuchsia::cobalt::Logger::LogCobaltEventCallback callback) override;
};

// Fail to acknowledge that LogEvent() was called and return |Status::INVALID_ARGUMENTS|.
class CobaltLoggerFailsLogEvent : public CobaltLoggerBase {
 public:
  // |fuchsia::cobalt::Logger|
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                fuchsia::cobalt::Logger::LogEventCallback callback) override;
};

// Will not execute the callback for the first n events.
class CobaltLoggerIgnoresFirstEvents : public CobaltLoggerBase {
 public:
  CobaltLoggerIgnoresFirstEvents(size_t n) : to_ignore_(n) {}

  // |fuchsia::cobalt::Logger|
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                fuchsia::cobalt::Logger::LogEventCallback callback) override;

 private:
  size_t to_ignore_;
  size_t num_calls_ = 0;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_H_
