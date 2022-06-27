// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/cobalt/logger.h"

#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include "src/cobalt/bin/utils/error_utils.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace cobalt {
namespace {

using async::PostDelayedTask;
using ::cobalt::ErrorToString;
using fxl::StringPrintf;

constexpr uint32_t kMaxPendingEvents = 500u;

uint64_t CurrentTimeUSecs(const timekeeper::Clock* clock) {
  return zx::nsec(clock->Now().get()).to_usecs();
}

}  // namespace

Logger::Logger(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               timekeeper::Clock* clock)
    : dispatcher_(dispatcher),
      services_(services),
      clock_(clock),
      logger_reconnection_backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u,
                                   /*max_delay=*/zx::hour(1)) {
  logger_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection with fuchsia.metrics.MetricEventLogger";
    RetryConnectingToLogger();
  });

  auto logger_request = logger_.NewRequest();
  ConnectToLogger(std::move(logger_request));
}

void Logger::ConnectToLogger(
    ::fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> logger_request) {
  // Connect to the LoggerFactory.
  logger_factory_ = services_->Connect<fuchsia::metrics::MetricEventLoggerFactory>();

  logger_factory_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection with fuchsia.metrics.MetricEventLoggerFactory";
  });

  // We don't need a long standing connection to the LoggerFactory so we unbind after setting up
  // the Logger.
  fuchsia::metrics::ProjectSpec project;
  project.set_customer_id(1);
  project.set_project_id(kProjectId);

  logger_factory_->CreateMetricEventLogger(
      std::move(project), std::move(logger_request),
      [this](fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLogger_Result status) {
        logger_factory_.Unbind();

        if (status.is_response()) {
          logger_reconnection_backoff_.Reset();
        } else if (status.err() == fuchsia::metrics::Error::SHUT_DOWN) {
          FX_LOGS(INFO) << "Stopping sending Cobalt events";
          logger_.Unbind();
        } else {
          FX_LOGS(WARNING) << "Failed to set up Cobalt: " << ErrorToString(status.err());
          logger_.Unbind();
          RetryConnectingToLogger();
        }
      });
}

void Logger::RetryConnectingToLogger() {
  if (logger_) {
    return;
  }

  // Bind |logger_| and immediately send the events that were not acknowledged by the server on the
  // previous connection.
  auto logger_request = logger_.NewRequest();
  SendAllPendingEvents();

  reconnect_task_.Reset([this, request = std::move(logger_request)]() mutable {
    ConnectToLogger(std::move(request));
  });

  PostDelayedTask(
      dispatcher_, [reconnect = reconnect_task_.callback()]() { reconnect(); },
      logger_reconnection_backoff_.GetNext());
}

void Logger::LogEvent(Event event) {
  if (pending_events_.size() >= kMaxPendingEvents) {
    FX_LOGS(INFO) << StringPrintf("Dropping Cobalt event %s - too many pending events (%lu)",
                                  event.ToString().c_str(), pending_events_.size());
    return;
  }

  const uint64_t event_id = next_event_id_++;
  pending_events_.insert(std::make_pair(event_id, std::move(event)));
  SendEvent(event_id);
}

uint64_t Logger::StartTimer() {
  const uint64_t timer_id = next_event_id_++;
  timer_starts_usecs_.insert(std::make_pair(timer_id, CurrentTimeUSecs(clock_)));
  return timer_id;
}

void Logger::SendEvent(uint64_t event_id) {
  if (!logger_) {
    return;
  }

  if (pending_events_.find(event_id) == pending_events_.end()) {
    return;
  }
  Event& event = pending_events_.at(event_id);

  auto callback = [this, event_id, &event](fpromise::result<void, fuchsia::metrics::Error> result) {
    if (result.is_error()) {
      FX_LOGS(INFO) << StringPrintf("Cobalt logging error: status %s, event %s",
                                    ErrorToString(result.error()).c_str(),
                                    event.ToString().c_str());
    }

    // We don't retry events that have been acknowledged by the server, regardless of the return
    // status.
    pending_events_.erase(event_id);
  };

  switch (event.type) {
    case EventType::kInteger:
      logger_->LogInteger(
          event.metric_id, event.count, event.dimensions,
          [callback = std::move(callback)](auto result) { callback(std::move(result)); });
      break;
    case EventType::kOccurrence:
      logger_->LogOccurrence(
          event.metric_id, event.count, event.dimensions,
          [callback = std::move(callback)](auto result) { callback(std::move(result)); });
      break;
  }
}

void Logger::SendAllPendingEvents() {
  for (const auto& [event_id, _] : pending_events_) {
    SendEvent(event_id);
  }
}

uint64_t Logger::GetTimerDurationUSecs(uint64_t timer_id) const {
  FX_CHECK(timer_starts_usecs_.find(timer_id) != timer_starts_usecs_.end());

  return CurrentTimeUSecs(clock_) - timer_starts_usecs_.at(timer_id);
}

}  // namespace cobalt
}  // namespace forensics
