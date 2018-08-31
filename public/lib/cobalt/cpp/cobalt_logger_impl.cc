// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/cobalt_logger_impl.h"
#include "garnet/public/lib/cobalt/cpp/cobalt_logger.h"

#include <set>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/callback/waiter.h>
#include <lib/component/cpp/connect.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

using fuchsia::cobalt::LoggerFactory;
using fuchsia::cobalt::ProjectProfile2;
using fuchsia::cobalt::Status2;

namespace cobalt {

CobaltLoggerImpl::CobaltLoggerImpl(async_dispatcher_t* dispatcher,
                                   component::StartupContext* context,
                                   ProjectProfile2 profile)
    : dispatcher_(dispatcher), context_(context), profile_(std::move(profile)) {
  ConnectToCobaltApplication();
}

CobaltLoggerImpl::~CobaltLoggerImpl() {
  if (!events_in_transit_.empty() || !events_to_send_.empty()) {
    FXL_LOG(WARNING) << "Disconnecting connection to cobalt with events "
                        "still pending... Events will be lost.";
  }
}

void CobaltLoggerImpl::LogEvent(uint32_t metric_id, uint32_t event_type_index) {
  LogEvent(std::make_unique<OccurrenceEvent>(metric_id, event_type_index));
}

void CobaltLoggerImpl::LogEventCount(uint32_t metric_id,
                                     uint32_t event_type_index,
                                     const std::string& component,
                                     zx::duration period_duration,
                                     int64_t count) {
  LogEvent(std::make_unique<CountEvent>(metric_id, event_type_index, component,
                                        period_duration.to_usecs(), count));
}

void CobaltLoggerImpl::LogElapsedTime(uint32_t metric_id,
                                      uint32_t event_type_index,
                                      const std::string& component,
                                      zx::duration elapsed_time) {
  LogEvent(std::make_unique<ElapsedTimeEvent>(
      metric_id, event_type_index, component, elapsed_time.to_usecs()));
}

void CobaltLoggerImpl::LogEvent(std::unique_ptr<Event> event) {
  if (dispatcher_ == async_get_default_dispatcher()) {
    LogEventOnMainThread(std::move(event));
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  async::PostTask(dispatcher_, [event = std::move(event), this]() mutable {
    this->LogEvent(std::move(event));
  });
}

void CobaltLoggerImpl::ConnectToCobaltApplication() {
  auto logger_factory = context_->ConnectToEnvironmentService<LoggerFactory>();

  ProjectProfile2 cloned_profile;
  FXL_CHECK(profile_.config.vmo.duplicate(
                ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                &cloned_profile.config.vmo) == ZX_OK)
      << "Could not clone config VMO";
  cloned_profile.config.size = profile_.config.size;

  logger_factory->CreateLogger(
      std::move(cloned_profile), logger_.NewRequest(), [this](Status2 status) {
        if (status == Status2::OK) {
          if (logger_) {
            logger_.set_error_handler([this]() { OnConnectionError(); });
            SendEvents();
          } else {
            OnConnectionError();
          }
        } else {
          FXL_LOG(ERROR) << "CreateLogger() received invalid arguments";
        }
      });
}

void CobaltLoggerImpl::OnTransitFail() {
  // Ugly way to move unique_ptrs between sets
  for (const auto& event : events_in_transit_) {
    events_to_send_.insert(
        std::move(const_cast<std::unique_ptr<Event>&>(event)));
  }

  events_in_transit_.clear();
}

void CobaltLoggerImpl::OnConnectionError() {
  FXL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  OnTransitFail();
  logger_.Unbind();
  async::PostDelayedTask(dispatcher_,
                         [this]() { ConnectToCobaltApplication(); },
                         backoff_.GetNext());
}

void CobaltLoggerImpl::LogEventOnMainThread(std::unique_ptr<Event> event) {
  events_to_send_.insert(std::move(event));
  if (!logger_ || !events_in_transit_.empty()) {
    return;
  }

  SendEvents();
}

void CobaltLoggerImpl::SendEvents() {
  FXL_DCHECK(events_in_transit_.empty());

  if (events_to_send_.empty()) {
    return;
  }

  events_in_transit_ = std::move(events_to_send_);
  events_to_send_.clear();

  auto waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();
  for (auto& event : events_in_transit_) {
    auto callback = waiter->NewCallback();
    event->Log(&logger_, [this, event_ptr = event.get(),
                          callback = std::move(callback)](Status2 status) {
      LogEventCallback(event_ptr, status);
      callback();
    });
  }

  waiter->Finalize([this]() {
    // No transient errors.
    if (events_in_transit_.empty()) {
      backoff_.Reset();
      // Send any event received while |events_in_transit_| was not
      // empty.
      SendEvents();
      return;
    }

    // A transient error happened, retry after a delay.
    async::PostDelayedTask(dispatcher_,
                           [this]() {
                             OnTransitFail();
                             SendEvents();
                           },
                           backoff_.GetNext());
  });
}

void CobaltLoggerImpl::LogEventCallback(const Event* event, Status2 status) {
  switch (status) {
    case Status2::INVALID_ARGUMENTS:
    case Status2::EVENT_TOO_BIG:  // fall through
      // Log the failure.
      FXL_LOG(WARNING) << "Cobalt rejected event for metric: "
                       << event->metric_id()
                       << " with status: " << fidl::ToUnderlying(status);
    case Status2::OK:  // fall through
      // Remove the event from the set of events to send.
      events_in_transit_.erase(std::lower_bound(
          events_in_transit_.begin(), events_in_transit_.end(), event,
          [](const std::unique_ptr<Event>& a, const Event* b) {
            return a.get() < b;
          }));
      break;
    case Status2::INTERNAL_ERROR:
    case Status2::BUFFER_FULL:
      // Keep the event for re-queueing.
      break;
  }
}

}  // namespace cobalt
