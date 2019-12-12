// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_
#define SRC_LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <set>

#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/fxl/logging.h"

namespace cobalt {

class BaseEvent {
 public:
  BaseEvent() {}
  virtual ~BaseEvent() = default;
  virtual void Log(fuchsia::cobalt::LoggerPtr* logger,
                   fit::function<void(fuchsia::cobalt::Status)> callback) = 0;

  virtual uint32_t metric_id() const { return 0; }
};

class Event : public BaseEvent {
 public:
  Event(uint32_t metric_id) : BaseEvent(), metric_id_(metric_id) {}
  virtual ~Event() = default;
  uint32_t metric_id() const override { return metric_id_; }

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
  CountEvent(uint32_t metric_id, uint32_t event_code, const std::string& component,
             int64_t period_duration_micros, int64_t count)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        period_duration_micros_(period_duration_micros),
        count_(count) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogEventCount(metric_id(), event_code_, component_, period_duration_micros_, count_,
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
  ElapsedTimeEvent(uint32_t metric_id, uint32_t event_code, const std::string& component,
                   int64_t elapsed_micros)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        elapsed_micros_(elapsed_micros) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogElapsedTime(metric_id(), event_code_, component_, elapsed_micros_,
                              std::move(callback));
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
  FrameRateEvent(uint32_t metric_id, uint32_t event_code, const std::string& component, float fps)
      : Event(metric_id), event_code_(event_code), component_(component), fps_(fps) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogFrameRate(metric_id(), event_code_, component_, fps_, std::move(callback));
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
  MemoryUsageEvent(uint32_t metric_id, uint32_t event_code, const std::string& component,
                   int64_t bytes)
      : Event(metric_id), event_code_(event_code), component_(component), bytes_(bytes) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->LogMemoryUsage(metric_id(), event_code_, component_, bytes_, std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  int64_t bytes() const { return bytes_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const int64_t bytes_;
};

class StartTimerEvent : public Event {
 public:
  StartTimerEvent(uint32_t metric_id, uint32_t event_code, const std::string& component,
                  const std::string& timer_id, uint64_t timestamp, uint32_t timeout_s)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        timer_id_(timer_id),
        timestamp_(timestamp),
        timeout_s_(timeout_s) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    (*logger)->StartTimer(metric_id(), event_code_, component_, timer_id_, timestamp_, timeout_s_,
                          std::move(callback));
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

class EndTimerEvent : public BaseEvent {
 public:
  EndTimerEvent(const std::string& timer_id, uint64_t timestamp, uint32_t timeout_s)
      : BaseEvent(), timer_id_(timer_id), timestamp_(timestamp), timeout_s_(timeout_s) {}
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
  IntHistogramEvent(uint32_t metric_id, uint32_t event_code, const std::string& component,
                    std::vector<fuchsia::cobalt::HistogramBucket> histogram)
      : Event(metric_id),
        event_code_(event_code),
        component_(component),
        histogram_(std::move(histogram)) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    std::vector<fuchsia::cobalt::HistogramBucket> histogram;
    FXL_CHECK(fidl::Clone(histogram_, &histogram) == ZX_OK);
    (*logger)->LogIntHistogram(metric_id(), event_code_, component_, std::move(histogram),
                               std::move(callback));
  }
  uint32_t event_code() const { return event_code_; }
  const std::string& component() const { return component_; }
  const std::vector<fuchsia::cobalt::HistogramBucket>& histogram() const { return histogram_; }

 private:
  const uint32_t event_code_;
  const std::string component_;
  const std::vector<fuchsia::cobalt::HistogramBucket> histogram_;
};

class CustomEvent : public Event {
 public:
  CustomEvent(uint32_t metric_id, std::vector<fuchsia::cobalt::CustomEventValue> event_values)
      : Event(metric_id), event_values_(std::move(event_values)) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    std::vector<fuchsia::cobalt::CustomEventValue> event_values;
    FXL_CHECK(fidl::Clone(event_values_, &event_values) == ZX_OK);
    (*logger)->LogCustomEvent(metric_id(), std::move(event_values), std::move(callback));
  }
  const std::vector<fuchsia::cobalt::CustomEventValue>& event_values() const {
    return event_values_;
  }

 private:
  const std::vector<fuchsia::cobalt::CustomEventValue> event_values_;
};

class CobaltEvent : public Event {
 public:
  CobaltEvent(fuchsia::cobalt::CobaltEvent event)
      : Event(event.metric_id), event_(std::move(event)) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    fuchsia::cobalt::CobaltEvent event;
    FXL_CHECK(fidl::Clone(event_, &event) == ZX_OK);
    (*logger)->LogCobaltEvent(std::move(event), std::move(callback));
  }

 private:
  const fuchsia::cobalt::CobaltEvent event_;
};

class CobaltEvents : public BaseEvent {
 public:
  CobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events)
      : BaseEvent(), events_(std::move(events)) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status)> callback) {
    std::vector<fuchsia::cobalt::CobaltEvent> events;
    FXL_CHECK(fidl::Clone(events_, &events) == ZX_OK);
    (*logger)->LogCobaltEvents(std::move(events), std::move(callback));
  }

 private:
  const std::vector<fuchsia::cobalt::CobaltEvent> events_;
};

class BaseCobaltLoggerImpl : public CobaltLogger {
 public:
  BaseCobaltLoggerImpl(async_dispatcher_t* dispatcher, std::string project_name,
                       uint32_t project_id, fuchsia::cobalt::ReleaseStage release_stage,
                       fuchsia::cobalt::ProjectProfile profile);
  ~BaseCobaltLoggerImpl() override;
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
                  const std::string& timer_id, zx::time timestamp, zx::duration timeout) override;
  void EndTimer(const std::string& timer_id, zx::time timestamp, zx::duration timeout) override;
  void LogIntHistogram(uint32_t metric_id, uint32_t event_code, const std::string& component,
                       std::vector<fuchsia::cobalt::HistogramBucket> histogram) override;
  void LogCustomEvent(uint32_t metric_id,
                      std::vector<fuchsia::cobalt::CustomEventValue> event_values) override;
  virtual void LogCobaltEvent(fuchsia::cobalt::CobaltEvent event) override;
  virtual void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> event) override;

 protected:
  void ConnectToCobaltApplication();
  virtual fidl::InterfacePtr<fuchsia::cobalt::LoggerFactory> ConnectToLoggerFactory() = 0;

 private:
  fuchsia::cobalt::ProjectProfile CloneProjectProfile();
  void OnConnectionError();
  void LogEventOnMainThread(std::unique_ptr<BaseEvent> event);
  void SendEvents();
  void OnTransitFail();
  void LogEventCallback(const BaseEvent* event, fuchsia::cobalt::Status status);
  void LogEvent(std::unique_ptr<BaseEvent> event);

  backoff::ExponentialBackoff backoff_;
  async_dispatcher_t* const dispatcher_;
  fuchsia::cobalt::LoggerPtr logger_;

  // This object is in one of two modes depending on which constructor was used.
  //
  // Mode 1: |project_name_| is non-empty or |project_id_| is non-zero. In this case |release_stage|
  // should also have been set, |profile_| is ignored, and when connecting to Cobalt we use
  // CreateLoggerFromProjectName() or CreateLoggerFromProjectId().
  //
  // Mode 2: |project_name_| is empty. In this case |profile_| should have been set,
  // |release_stage_| is ignored, and when connecting to Cobalt we use CreateLogger().
  const std::string project_name_;
  const uint32_t project_id_;
  const fuchsia::cobalt::ReleaseStage release_stage_ = fuchsia::cobalt::ReleaseStage::GA;
  const fuchsia::cobalt::ProjectProfile profile_;

  std::set<std::unique_ptr<BaseEvent>> events_to_send_;
  std::set<std::unique_ptr<BaseEvent>> events_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BaseCobaltLoggerImpl);
};

class CobaltLoggerImpl : public BaseCobaltLoggerImpl {
 public:
  // Use this version of the constructor in order to connect to the Cobalt
  // application via CreateLogger().
  CobaltLoggerImpl(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                   fuchsia::cobalt::ProjectProfile profile);

  // Use this version of the constructor in order to connect to the Cobalt
  // application via CreateLoggerFromProjectName().
  // DEPRECATED: use the alternative below that accepts a project ID.
  CobaltLoggerImpl(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                   std::string project_name, fuchsia::cobalt::ReleaseStage release_stage);

  // Use this version of the constructor in order to connect to the Cobalt
  // application via CreateLoggerFromProjectId().
  CobaltLoggerImpl(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                   uint32_t project_id);

  ~CobaltLoggerImpl() override{};

 protected:
  virtual fidl::InterfacePtr<fuchsia::cobalt::LoggerFactory> ConnectToLoggerFactory() override;

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltLoggerImpl);
};

}  // namespace cobalt

#endif  // SRC_LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_
