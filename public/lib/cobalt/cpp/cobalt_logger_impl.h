// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_
#define LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_

#include "garnet/public/lib/cobalt/cpp/cobalt_logger.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>
#include <zx/time.h>

namespace cobalt {

class Event {
 public:
  Event(uint32_t metric_id) : metric_id_(metric_id) {}
  virtual ~Event() = default;
  virtual void Log(fuchsia::cobalt::LoggerPtr* logger,
                   fit::function<void(fuchsia::cobalt::Status)> callback) = 0;
  uint32_t metric_id() const { return metric_id_; }

 private:
  const uint32_t metric_id_;
};

class OccurrenceEvent : public Event {
 public:
  OccurrenceEvent(uint32_t metric_id, uint32_t event_code)
      : Event(metric_id), event_code_(event_code) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogEvent(metric_id(), event_code_, std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }

 private:
  const uint32_t event_code_;
};

class CountEvent : public Event {
 public:
  CountEvent(uint32_t metric_id, uint32_t event_code,
             const std::string& component, int64_t period_duration_micros,
             int64_t count)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        period_duration_micros_(period_duration_micros),
        count_(count) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogEventCount(metric_id(), event_code_, component_,
                             period_duration_micros_, count_,
                             std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  int64_t period_duration_micros() const { return period_duration_micros_; }
  int64_t count() const { return count_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const int64_t period_duration_micros_;
  const int64_t count_;
};

class ElapsedTimeEvent : public Event {
 public:
  ElapsedTimeEvent(uint32_t metric_id, uint32_t event_code,
                   const std::string& component, int64_t elapsed_micros)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        elapsed_micros_(elapsed_micros) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogElapsedTime(metric_id(), event_code_, component_,
                              elapsed_micros_, std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  int64_t elapsed_micros() const { return elapsed_micros_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const int64_t elapsed_micros_;
};

class FrameRateEvent : public Event {
 public:
  FrameRateEvent(uint32_t metric_id, uint32_t event_code,
                 const std::string& component, float fps)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        fps_(fps) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogFrameRate(metric_id(), event_code_, component_, fps_,
                            std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  float fps() const { return fps_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const float fps_;
};

class MemoryUsageEvent : public Event {
 public:
  MemoryUsageEvent(uint32_t metric_id, uint32_t event_code,
                   const std::string& component, int64_t bytes)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        bytes_(bytes) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogMemoryUsage(metric_id(), event_code_, component_, bytes_,
                              std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  int64_t bytes() const { return bytes_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const int64_t bytes_;
};

class StringUsedEvent : public Event {
 public:
  StringUsedEvent(uint32_t metric_id, const std::string& s)
      : Event(metric_id), s_(s) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogString(metric_id(), s_, std::move(callback));
  }
  const std::string& s() const { return s_; }

 private:
  const std::string s_;
};

class StartTimerEvent : public Event {
 public:
  StartTimerEvent(uint32_t metric_id, uint32_t event_code,
                  const std::string& component, const std::string& timer_id,
                  uint64_t timestamp, uint32_t timeout_s)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        timer_id_(timer_id),
        timestamp_(timestamp),
        timeout_s_(timeout_s) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->StartTimer(metric_id(), event_code_, component_, timer_id_,
                          timestamp_, timeout_s_, std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  const std::string& timer_id() const { return timer_id_; }
  uint64_t timestamp() const { return timestamp_; }
  uint32_t timeout_s() const { return timeout_s_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const std::string timer_id_;
  const uint64_t timestamp_;
  const uint32_t timeout_s_;
};

class EndTimerEvent : public Event {
 public:
  EndTimerEvent(const std::string& timer_id, uint64_t timestamp,
                uint32_t timeout_s)
      : Event(0),
        timer_id_(timer_id),
        timestamp_(timestamp),
        timeout_s_(timeout_s) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->EndTimer(timer_id_, timestamp_, timeout_s_, std::move(callback));
  }
  const std::string& timer_id() const { return timer_id_; }
  uint64_t timestamp() const { return timestamp_; }
  uint32_t timeout_s() const { return timeout_s_; }

 private:
  const std::string timer_id_;
  const uint64_t timestamp_;
  const uint32_t timeout_s_;
};

class IntHistogramEvent : public Event {
 public:
  IntHistogramEvent(uint32_t metric_id, uint32_t event_code,
                    const std::string& component,
                    fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        histogram_(std::move(histogram)) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram;
    FXL_CHECK(fidl::Clone(histogram_, &histogram) == ZX_OK);
    (*logger)->LogIntHistogram(metric_id(), event_code_, component_,
                               std::move(histogram), std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  const fidl::VectorPtr<fuchsia::cobalt::HistogramBucket>& histogram() const {
    return histogram_;
  }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram_;
};

class CustomEvent : public Event {
 public:
  CustomEvent(uint32_t metric_id,
              fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values)
      : Event(metric_id), event_values_(std::move(event_values)) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values;
    FXL_CHECK(fidl::Clone(event_values_, &event_values) == ZX_OK);
    (*logger)->LogCustomEvent(metric_id(), std::move(event_values),
                              std::move(callback));
  }
  const fidl::VectorPtr<fuchsia::cobalt::CustomEventValue>& event_values()
      const {
    return event_values_;
  }

 private:
  const fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values_;
};

class CobaltLoggerImpl : public CobaltLogger {
 public:
  CobaltLoggerImpl(async_dispatcher_t* dispatcher,
                   component::StartupContext* context,
                   fuchsia::cobalt::ProjectProfile profile);
  ~CobaltLoggerImpl() override;
  void LogEvent(uint32_t metric_id, uint32_t event_code) override;
  void LogEventCount(uint32_t metric_id, uint32_t event_code,
                     const std::string& component, zx::duration period_duration,
                     int64_t count) override;
  void LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                      const std::string& component,
                      zx::duration elapsed_time) override;
  void LogFrameRate(uint32_t metric_id, uint32_t event_code,
                    const std::string& component, float fps) override;
  void LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                      const std::string& component, int64_t bytes) override;
  void LogString(uint32_t metric_id, const std::string& s) override;
  void StartTimer(uint32_t metric_id, uint32_t event_code,
                  const std::string& component, const std::string& timer_id,
                  zx::time timestamp, zx::duration timeout) override;
  void EndTimer(const std::string& timer_id, zx::time timestamp,
                zx::duration timeout) override;
  void LogIntHistogram(
      uint32_t metric_id, uint32_t event_code, const std::string& component,
      std::vector<fuchsia::cobalt::HistogramBucket> histogram) override;
  void LogCustomEvent(
      uint32_t metric_id,
      std::vector<fuchsia::cobalt::CustomEventValue> event_values) override;

 private:
  void ConnectToCobaltApplication();
  fuchsia::cobalt::ProjectProfile CloneProjectProfile();
  void OnConnectionError();
  void LogEventOnMainThread(std::unique_ptr<Event> event);
  void SendEvents();
  void OnTransitFail();
  void LogEventCallback(const Event* event, fuchsia::cobalt::Status status);
  void LogEvent(std::unique_ptr<Event> event);

  backoff::ExponentialBackoff backoff_;
  async_dispatcher_t* const dispatcher_;
  component::StartupContext* context_;
  fuchsia::cobalt::LoggerPtr logger_;
  const fuchsia::cobalt::ProjectProfile profile_;
  std::set<std::unique_ptr<Event>> events_to_send_;
  std::set<std::unique_ptr<Event>> events_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltLoggerImpl);
};

}  // namespace cobalt

#endif  // LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_
