// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_LOGGER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <deque>
#include <memory>

namespace feedback {

// Log events to cobalt.
class Cobalt {
 public:
  // We expect fuchsia.cobalt.LoggerFactory to be in |services|.
  Cobalt(std::shared_ptr<sys::ServiceDirectory> services);

  // Log an event with fuchsia.cobalt.Logger with the provided parameters. If the service is not
  // accessible, keep the parameters to try again later.
  void Log(
      uint32_t metric_id, uint32_t event_code,
      fit::callback<void(fuchsia::cobalt::Status)> callback = [](fuchsia::cobalt::Status) {});

 private:
  struct Event {
    Event(uint32_t metric_id, uint32_t event_code,
          fit::callback<void(fuchsia::cobalt::Status)> callback)
        : metric_id(metric_id), event_code(event_code), callback(std::move(callback)) {}

    // Make this object move only
    Event(const Event& other) = delete;
    Event& operator=(const Event& other) = delete;
    Event(Event&& other) = default;
    Event& operator=(Event&& other) = default;

    uint32_t metric_id = 0;
    uint32_t event_code = 0;
    fit::callback<void(fuchsia::cobalt::Status)> callback;
    std::string ToString() const;
  };

  void SetUpLogger();
  void Log(Event event);
  void FlushPendingEvents();

  std::shared_ptr<sys::ServiceDirectory> services_;

  fuchsia::cobalt::LoggerFactoryPtr logger_factory_;
  fuchsia::cobalt::LoggerPtr logger_;
  std::deque<Event> earliest_pending_events_;

  bool can_log_event_ = false;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_UTILS_COBALT_LOGGER_H_
