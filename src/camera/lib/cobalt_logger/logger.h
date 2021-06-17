// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_COBALT_LOGGER_LOGGER_H_
#define SRC_CAMERA_LIB_COBALT_LOGGER_LOGGER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <any>
#include <map>

#include "src/camera/lib/cobalt_logger/event.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace camera::cobalt {

template <typename T>
constexpr auto ToIntegral(T t) -> typename std::underlying_type<T>::type {
  return static_cast<typename std::underlying_type<T>::type>(t);
}

// Log events to cobalt.
class Logger {
 public:
  // We expect fuchsia.metrics.MetricEventLoggerFactory to be in |services|.
  Logger(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
         std::unique_ptr<timekeeper::Clock> clock = std::make_unique<timekeeper::SystemClock>());

  virtual ~Logger() = default;

  // Log event with no dimensions.
  virtual void LogInteger(uint32_t metric_id, std::vector<uint32_t> dimensions, uint64_t value) {
    LogEvent(CameraEvent(EventType::kInteger, metric_id, std::move(dimensions), value));
  }

  // Log event with fuchsia.metrics.MetricEventLogger with the provided parameters. If the service
  // is not accessible, keep the parameters to try again later.
  virtual void LogOccurrence(uint32_t metric_id, std::vector<uint32_t> dimensions) {
    LogEvent(CameraEvent(EventType::kOccurrence, metric_id, std::move(dimensions), 1lu));
  }

  // Start a timer and return the id to that timer. The id is needed to log the elapsed time since
  // starting the timer.
  uint64_t StartTimer();

  // Log the time elapsed in microseconds since starting the timer with id |timer_id| with
  // fuchsia.metrics.MetricEventLogger. If the service is not accessible, keep the parameters to
  // try again later.
  //
  // This does not stop the timer.
  virtual void LogElapsedTime(uint32_t metric_id, std::vector<uint32_t> dimensions,
                              uint64_t timer_id) {
    LogEvent(CameraEvent(EventType::kInteger, metric_id, std::move(dimensions),
                         GetTimerDurationUSecs(timer_id)));
  }

  // Log a duration in microseconds. If the service is not accessible, keep the parameters to try
  // again later.
  virtual void LogDuration(uint32_t metric_id, std::vector<uint32_t> dimensions,
                           zx::duration duration) {
    LogEvent(
        CameraEvent(EventType::kInteger, metric_id, std::move(dimensions), duration.to_usecs()));
  }

  // Immediately shutdown |Logger| so it can no longer be used to log events.
  void Shutdown();

  template <typename DimensionType, typename... RestDimensionTypes>
  static std::vector<uint32_t> BuildDimension(DimensionType dimension, RestDimensionTypes... rest) {
    std::vector<uint32_t> output;
    AddDimensions(output, dimension, rest...);
    return output;
  }

  static StreamType ConvertStreamType(fuchsia::camera2::CameraStreamType type);

 protected:
  virtual void LogEvent(Event event);

 private:
  template <typename DimensionType>
  static void AddDimensions(std::vector<uint32_t>& output, DimensionType dimension) {
    output.push_back(ToIntegral(dimension));
  }

  template <typename DimensionType, typename... RestDimensionTypes>
  static void AddDimensions(std::vector<uint32_t>& output, DimensionType dimension,
                            RestDimensionTypes... rest) {
    output.push_back(ToIntegral(dimension));
    AddDimensions(output, rest...);
  }

  void ConnectToLogger(
      ::fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> logger_request);
  void RetryConnectingToLogger();
  void SendEvent(uint64_t event_id);
  void SendAllPendingEvents();
  uint64_t GetTimerDurationUSecs(uint64_t timer_id) const;

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<timekeeper::Clock> clock_;

  fuchsia::metrics::MetricEventLoggerFactoryPtr logger_factory_;
  fuchsia::metrics::MetricEventLoggerPtr logger_;

  // An event is pending if it has been written into a channel, but has not been acknowledged by
  // the recipient.
  std::map<uint64_t, Event> pending_events_;
  std::map<uint64_t, uint64_t> timer_starts_usecs_;
  backoff::ExponentialBackoff logger_reconnection_backoff_;

  // We need to be able to cancel a posted reconnection task when |Logger| is destroyed.
  fxl::CancelableClosure reconnect_task_;

  uint64_t next_event_id_ = 0;
  bool shut_down_ = false;
};

}  // namespace camera::cobalt

#endif  // SRC_CAMERA_LIB_COBALT_LOGGER_LOGGER_H_
