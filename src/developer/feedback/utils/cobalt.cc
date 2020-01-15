// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/cobalt.h"

#include "lib/async/cpp/task.h"
#include "lib/zx/time.h"
#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using async::PostDelayedTask;
using fuchsia::cobalt::LoggerFactory;
using fuchsia::cobalt::Status;
using fxl::StringPrintf;

constexpr uint32_t kMaxQueueSize = 500u;

// Convert a status to a string.
std::string ToString(const Status& status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case Status::EVENT_TOO_BIG:
      return "EVENT_TO_BIG";
    case Status::BUFFER_FULL:
      return "BUFFER_FULL";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
}

}  // namespace

Cobalt::Cobalt(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(services),
      logger_reconnection_backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u,
                                   /*max_delay=*/zx::hour(1)) {
  logger_factory_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Error with fuchsia.cobalt.LoggerFactory";
  });

  logger_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Error with fuchsia.cobalt.Logger";
    RetryConnectingToLogger();
  });

  auto logger_request = logger_.NewRequest();
  ConnectToLogger(std::move(logger_request));
}

void Cobalt::ConnectToLogger(fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger_request) {
  // Connect to the LoggerFactory.
  logger_factory_ = services_->Connect<LoggerFactory>();

  // We don't need a long standing connection to the LoggerFactory so we unbind afer setting up the
  // Logger.
  logger_factory_->CreateLoggerFromProjectId(
      kProjectId, std::move(logger_request), [this](Status status) {
        logger_factory_.Unbind();

        if (status == Status::OK) {
          logger_reconnection_backoff_.Reset();
        } else {
          FX_LOGS(ERROR) << "Failed to set up Cobalt: " << ToString(status);
          logger_.Unbind();
          RetryConnectingToLogger();
        }
      });
}

void Cobalt::RetryConnectingToLogger() {
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

void Cobalt::LogEvent(CobaltEvent event, fit::callback<void(fuchsia::cobalt::Status)> callback) {
  if (pending_events_.size() >= kMaxQueueSize) {
    FX_LOGS(INFO) << StringPrintf("Dropping Cobalt event %s - too many pending events (%lu)",
                                  event.ToString().c_str(), pending_events_.size());
    return;
  }

  const auto event_id = next_event_id_++;
  pending_events_.insert(std::make_pair(event_id, PendingEvent(event, std::move(callback))));
  SendEvent(event_id);
}

void Cobalt::SendEvent(uint64_t event_id) {
  if (!logger_) {
    return;
  }

  if (pending_events_.find(event_id) == pending_events_.end()) {
    return;
  }
  auto& pending_event = pending_events_.at(event_id);

  auto cb = [this, event_id, &pending_event](Status status) {
    if (status != Status::OK) {
      FX_LOGS(INFO) << StringPrintf("Cobalt logging error: status %s, event %s",
                                    ToString(status).c_str(),
                                    pending_event.event.ToString().c_str());
    }

    pending_event.callback(status);

    // We don't retry events that have been acknowledged by the server, regardless of the return
    // status.
    pending_events_.erase(event_id);
  };

  const auto& event = pending_event.event;
  switch (event.type) {
    case CobaltEvent::Type::Occurrence:
      logger_->LogEvent(event.metric_id, event.event_code, std::move(cb));
      break;
    case CobaltEvent::Type::Count:
      logger_->LogEventCount(event.metric_id, event.event_code, /*component=*/"",
                             /*period_duration_micros=*/0u, event.count, std::move(cb));
      break;
  }
}

void Cobalt::SendAllPendingEvents() {
  for (const auto& [event_id, _] : pending_events_) {
    SendEvent(event_id);
  }
}

}  // namespace feedback
