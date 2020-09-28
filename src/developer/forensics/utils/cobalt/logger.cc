// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/cobalt/logger.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include "src/cobalt/bin/utils/status_utils.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace cobalt {
namespace {

using async::PostDelayedTask;
using ::cobalt::StatusToString;
using fuchsia::cobalt::LoggerFactory;
using fuchsia::cobalt::Status;
using fxl::StringPrintf;

constexpr uint32_t kMaxPendingEvents = 500u;

uint64_t CurrentTimeUSecs(const std::unique_ptr<timekeeper::Clock>& clock) {
  return zx::nsec(clock->Now().get()).to_usecs();
}

}  // namespace

Logger::Logger(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               std::unique_ptr<timekeeper::Clock> clock)
    : dispatcher_(dispatcher),
      services_(services),
      clock_(std::move(clock)),
      logger_reconnection_backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u,
                                   /*max_delay=*/zx::hour(1)) {
  logger_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Error with fuchsia.cobalt.Logger";
    RetryConnectingToLogger();
  });

  auto logger_request = logger_.NewRequest();
  ConnectToLogger(std::move(logger_request));
}

void Logger::Shutdown() {
  shut_down_ = true;

  pending_events_.clear();
  timer_starts_usecs_.clear();

  reconnect_task_.Cancel();

  logger_factory_.Unbind();
  logger_.Unbind();
}

void Logger::ConnectToLogger(::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger_request) {
  // Connect to the LoggerFactory.
  logger_factory_ = services_->Connect<LoggerFactory>();

  logger_factory_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Error with fuchsia.cobalt.LoggerFactory";
  });

  // We don't need a long standing connection to the LoggerFactory so we unbind afer setting up the
  // Logger.
  logger_factory_->CreateLoggerFromProjectId(
      kProjectId, std::move(logger_request), [this](Status status) {
        logger_factory_.Unbind();

        if (status == Status::OK) {
          logger_reconnection_backoff_.Reset();
        } else {
          FX_LOGS(ERROR) << "Failed to set up Logger: " << StatusToString(status);
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
  FX_CHECK(!shut_down_);
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
  FX_CHECK(!shut_down_);

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

  auto cb = [this, event_id, &event](Status status) {
    if (status != Status::OK) {
      FX_LOGS(INFO) << StringPrintf("Cobalt logging error: status %s, event %s",
                                    StatusToString(status).c_str(), event.ToString().c_str());
    }

    // We don't retry events that have been acknowledged by the server, regardless of the return
    // status.
    pending_events_.erase(event_id);
  };

  switch (event.type) {
    case EventType::kOccurrence:
      logger_->LogEvent(event.metric_id, event.dimensions[0], std::move(cb));
      break;
    case EventType::kCount:
      logger_->LogEventCount(event.metric_id, event.dimensions[0], /*component=*/"",
                             /*period_duration_micros=*/0u, event.count, std::move(cb));
      break;
    case EventType::kTimeElapsed:
      logger_->LogElapsedTime(event.metric_id, event.dimensions[0], /*component=*/"",
                              /*elapsed_micros=*/event.usecs_elapsed, std::move(cb));
      break;
    case EventType::kMultidimensionalOccurrence:
      fuchsia::cobalt::CobaltEvent cobalt_event;
      cobalt_event.metric_id = event.metric_id;
      cobalt_event.event_codes = event.dimensions;
      cobalt_event.payload = fuchsia::cobalt::EventPayload::WithEventCount(
          fuchsia::cobalt::CountEvent{.count = static_cast<int64_t>(event.count)});

      logger_->LogCobaltEvent(std::move(cobalt_event), std::move(cb));
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
