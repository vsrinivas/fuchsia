// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_LOGGER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_LOGGER_H_

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

namespace feedback {

// Log events to cobalt.
class Cobalt {
 public:
  // We expect fuchsia.cobalt.LoggerFactory to be in |services|.
  Cobalt(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  // Log an occurrence event with fuchsia.cobalt.Logger with the provided parameters. If the service
  // is not accessible, keep the parameters to try again later.
  void LogOccurrence(
      uint32_t metric_id, uint32_t event_code,
      fit::callback<void(fuchsia::cobalt::Status)> callback = [](fuchsia::cobalt::Status) {});

  // Log a count event with fuchsia.cobalt.Logger with the provided parameters. If the service is
  // not accessible, keep the parameters to try again later.
  void LogCount(
      uint32_t metric_id, uint32_t event_code, uint64_t count,
      fit::callback<void(fuchsia::cobalt::Status)> callback = [](fuchsia::cobalt::Status) {});

 private:
  struct PendingEvent {
    PendingEvent(CobaltEvent event, fit::callback<void(fuchsia::cobalt::Status)> callback)
        : event(event), callback(std::move(callback)) {}
    CobaltEvent event;
    fit::callback<void(fuchsia::cobalt::Status)> callback;
  };

  void ConnectToLogger(fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger_request);
  void RetryConnectingToLogger();
  void LogEvent(CobaltEvent event, fit::callback<void(fuchsia::cobalt::Status)> callback);
  void SendEvent(uint64_t event_id);
  void SendAllPendingEvents();

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;

  fuchsia::cobalt::LoggerFactoryPtr logger_factory_;
  fuchsia::cobalt::LoggerPtr logger_;

  // An event is pending if it has been written into a channel, but has not been acknowledged by
  // the recipient.
  std::map<uint64_t, PendingEvent> pending_events_;
  backoff::ExponentialBackoff logger_reconnection_backoff_;

  uint64_t next_event_id_ = 0;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_LOGGER_H_
