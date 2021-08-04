// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_COBALT_LOGGER_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_COBALT_LOGGER_H_

#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {
namespace cobalt {

// Log events to cobalt.
class Logger {
 public:
  // We expect fuchsia.metrics.MetricEventLoggerFactory to be in |services|.
  Logger(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
         timekeeper::Clock* clock);

  // Log event with no dimensions.
  void LogIntegerEvent(uint32_t metric_id, uint64_t count) {
    LogEvent(Event(cobalt::EventType::kInteger, metric_id, {}, count));
  }

  // Log event with fuchsia.metrics.MetricEventLogger with the provided parameters. If the service
  // is not accessible, keep the parameters to try again later.
  template <typename... DimensionTypes>
  void LogOccurrence(DimensionTypes... dimensions) {
    LogEvent(Event(std::forward<DimensionTypes>(dimensions)...));
  }

  // Log event with fuchsia.metrics.MetricEventLogger with the provided parameters. If the service
  // is not accessible, keep the parameters to try again later.
  template <typename DimensionType>
  void LogCount(DimensionType dimension, uint64_t count) {
    LogEvent(Event(dimension, count));
  }

  // Start a timer and return the id to that timer. The id is needed to log the elapsed time since
  // starting the timer.
  uint64_t StartTimer();

  // Log the time elapsed in microseconds since starting the timer with id |timer_id| with
  // fuchsia.metrics.MetricEventLogger. If the service is not accessible, keep the parameters to
  // try again later.
  //
  // This does not stop the timer.
  template <typename DimensionType>
  void LogElapsedTime(DimensionType dimension, uint64_t timer_id) {
    LogEvent(Event(dimension, GetTimerDurationUSecs(timer_id)));
  }

  // Log a duration in microseconds. If the service is not accessible, keep the parameters to try
  // again later.
  template <typename DimensionType>
  void LogDuration(DimensionType dimension, zx::duration duration) {
    LogEvent(Event(dimension, duration.to_usecs()));
  }

 private:
  void ConnectToLogger(
      ::fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> logger_request);
  void RetryConnectingToLogger();
  void LogEvent(Event event);
  void SendEvent(uint64_t event_id);
  void SendAllPendingEvents();
  uint64_t GetTimerDurationUSecs(uint64_t timer_id) const;

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;

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
};

}  // namespace cobalt
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_COBALT_LOGGER_H_
