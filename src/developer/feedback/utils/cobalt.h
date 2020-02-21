// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include "src/developer/feedback/utils/cobalt_event.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace feedback {

// Log events to cobalt.
class Cobalt {
 public:
  // We expect fuchsia.cobalt.LoggerFactory to be in |services|.
  Cobalt(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
         std::unique_ptr<timekeeper::Clock> clock = std::make_unique<timekeeper::SystemClock>());

  // Log an occurrence event with fuchsia.cobalt.Logger with the provided parameters. If the service
  // is not accessible, keep the parameters to try again later.
  template <typename EventCodeType>
  void LogOccurrence(EventCodeType event_code) {
    LogEvent(CobaltEvent(event_code));
  }

  // Log a count event with fuchsia.cobalt.Logger with the provided parameters. If the service is
  // not accessible, keep the parameters to try again later.
  template <typename EventCodeType>
  void LogCount(EventCodeType event_code, uint64_t count) {
    LogEvent(CobaltEvent(event_code, count));
  }

  // Start a timer and return the id to that timer. The id is needed to log the elapsed time since
  // starting the timer.
  uint64_t StartTimer();

  // Log the time elapsed in microseconds since starting the timer with id |timer_id| with
  // fuchsia.cobalt.Logger. If the service is not accessible, keep the parameters to try again
  // later.
  //
  // This does not stop the timer.
  template <typename EventCodeType>
  void LogElapsedTime(EventCodeType event_code, uint64_t timer_id) {
    LogEvent(CobaltEvent(event_code, GetTimerDurationUSecs(timer_id)));
  }

  // Immediately shutdown |Cobalt| so it can no longer be used to log events.
  void Shutdown();

 private:
  void ConnectToLogger(fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger_request);
  void RetryConnectingToLogger();
  void LogEvent(CobaltEvent event);
  void SendEvent(uint64_t event_id);
  void SendAllPendingEvents();
  uint64_t GetTimerDurationUSecs(uint64_t timer_id) const;

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<timekeeper::Clock> clock_;

  fuchsia::cobalt::LoggerFactoryPtr logger_factory_;
  fuchsia::cobalt::LoggerPtr logger_;

  // An event is pending if it has been written into a channel, but has not been acknowledged by
  // the recipient.
  std::map<uint64_t, CobaltEvent> pending_events_;
  std::map<uint64_t, uint64_t> timer_starts_usecs_;
  backoff::ExponentialBackoff logger_reconnection_backoff_;

  // We need to be able to cancel a posted reconnection task when |Cobalt| is destroyed.
  fxl::CancelableClosure reconnect_task_;

  uint64_t next_event_id_ = 0;
  bool shut_down_ = false;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_COBALT_H_
