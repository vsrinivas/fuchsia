// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_
#define GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_

#include "garnet/public/lib/cobalt/cpp/cobalt_logger.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fit/function.h>

namespace cobalt {

class Event {
 public:
  Event(uint32_t metric_id) : metric_id_(metric_id) {}
  virtual ~Event() = default;
  virtual void Log(fuchsia::cobalt::LoggerPtr* logger,
                   fit::function<void(fuchsia::cobalt::Status2)> callback) = 0;
  uint32_t metric_id() const { return metric_id_; }

 private:
  const uint32_t metric_id_;
};

class OccurrenceEvent : public Event {
 public:
  OccurrenceEvent(uint32_t metric_id, uint32_t event_type_index)
      : Event(metric_id), event_type_index_(event_type_index) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status2)> callback) {
    (*logger)->LogEvent(metric_id(), event_type_index_, std::move(callback));
  }
  uint32_t event_type_index() const { return event_type_index_; }

 private:
  const uint32_t event_type_index_;
};

class CountEvent : public Event {
 public:
  CountEvent(uint32_t metric_id, uint32_t event_type_index,
             const std::string& component, int64_t period_duration_micros,
             int64_t count)
      : Event(metric_id),
        event_type_index_(event_type_index),
        component_(component),
        period_duration_micros_(period_duration_micros),
        count_(count) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status2)> callback) {
    (*logger)->LogEventCount(metric_id(), event_type_index_, component_,
                             period_duration_micros_, count_,
                             std::move(callback));
  }
  uint32_t event_type_index() const { return event_type_index_; }
  const std::string& component() const { return component_; }
  int64_t period_duration_micros() const { return period_duration_micros_; }
  int64_t count() const { return count_; }

 private:
  const uint32_t event_type_index_;
  const std::string component_;
  const int64_t period_duration_micros_;
  const int64_t count_;
};

class ElapsedTimeEvent : public Event {
 public:
  ElapsedTimeEvent(uint32_t metric_id, uint32_t event_type_index,
                   const std::string& component, int64_t elapsed_micros)
      : Event(metric_id),
        event_type_index_(event_type_index),
        component_(component),
        elapsed_micros_(elapsed_micros) {}
  void Log(fuchsia::cobalt::LoggerPtr* logger,
           fit::function<void(fuchsia::cobalt::Status2)> callback) {
    (*logger)->LogElapsedTime(metric_id(), event_type_index_, component_,
                              elapsed_micros_, std::move(callback));
  }
  uint32_t event_type_index() const { return event_type_index_; }
  const std::string& component() const { return component_; }
  int64_t elapsed_micros() const { return elapsed_micros_; }

 private:
  const uint32_t event_type_index_;
  const std::string component_;
  const int64_t elapsed_micros_;
};

class CobaltLoggerImpl : public CobaltLogger {
 public:
  CobaltLoggerImpl(async_dispatcher_t* dispatcher,
                   component::StartupContext* context,
                   fuchsia::cobalt::ProjectProfile2 profile);
  ~CobaltLoggerImpl() override;
  void LogEvent(uint32_t metric_id, uint32_t event_type_index) override;
  void LogEventCount(uint32_t metric_id, uint32_t event_type_index,
                     const std::string& component, zx::duration period_duration,
                     int64_t count) override;
  void LogElapsedTime(uint32_t metric_id, uint32_t event_type_index,
                      const std::string& component,
                      zx::duration elapsed_time) override;

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void LogEventOnMainThread(std::unique_ptr<Event> event);
  void SendEvents();
  void OnTransitFail();
  void LogEventCallback(const Event* event, fuchsia::cobalt::Status2 status);
  void LogEvent(std::unique_ptr<Event> event);

  backoff::ExponentialBackoff backoff_;
  async_dispatcher_t* const dispatcher_;
  component::StartupContext* context_;
  fuchsia::cobalt::LoggerPtr logger_;
  const fuchsia::cobalt::ProjectProfile2 profile_;
  std::set<std::unique_ptr<Event>> events_to_send_;
  std::set<std::unique_ptr<Event>> events_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltLoggerImpl);
};

}  // namespace cobalt

#endif  // GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_IMPL_H_
