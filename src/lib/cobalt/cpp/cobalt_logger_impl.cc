// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/cobalt_logger_impl.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <set>

#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/cobalt/cpp/cobalt_logger.h"

using fuchsia::cobalt::LoggerFactory;
using fuchsia::cobalt::Status;

namespace cobalt {

BaseCobaltLoggerImpl::BaseCobaltLoggerImpl(async_dispatcher_t* dispatcher, uint32_t project_id)
    : dispatcher_(dispatcher), project_id_(project_id) {
  FX_CHECK(project_id_ > 0) << "Must define a project_id greater than 0.";
}

BaseCobaltLoggerImpl::~BaseCobaltLoggerImpl() {
  if (!events_in_transit_.empty() || !events_to_send_.empty()) {
    FX_LOGS(WARNING) << "Disconnecting connection to cobalt with events "
                        "still pending... Events will be lost.";
  }
}

void BaseCobaltLoggerImpl::LogEvent(uint32_t metric_id, uint32_t event_code) {
  LogEvent(std::make_unique<OccurrenceEvent>(metric_id, event_code));
}

void BaseCobaltLoggerImpl::LogEventCount(uint32_t metric_id, uint32_t event_code,
                                         const std::string& component, zx::duration period_duration,
                                         int64_t count) {
  LogEvent(std::make_unique<CountEvent>(metric_id, event_code, component,
                                        period_duration.to_usecs(), count));
}

void BaseCobaltLoggerImpl::LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                                          const std::string& component, zx::duration elapsed_time) {
  LogEvent(std::make_unique<ElapsedTimeEvent>(metric_id, event_code, component,
                                              elapsed_time.to_usecs()));
}

void BaseCobaltLoggerImpl::LogFrameRate(uint32_t metric_id, uint32_t event_code,
                                        const std::string& component, float fps) {
  LogEvent(std::make_unique<FrameRateEvent>(metric_id, event_code, component, fps));
}

void BaseCobaltLoggerImpl::LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                                          const std::string& component, int64_t bytes) {
  LogEvent(std::make_unique<MemoryUsageEvent>(metric_id, event_code, component, bytes));
}

void BaseCobaltLoggerImpl::StartTimer(uint32_t metric_id, uint32_t event_code,
                                      const std::string& component, const std::string& timer_id,
                                      zx::time timestamp, zx::duration timeout) {
  LogEvent(std::make_unique<StartTimerEvent>(metric_id, event_code, component, timer_id,
                                             timestamp.get() / ZX_USEC(1), timeout.to_secs()));
}

void BaseCobaltLoggerImpl::EndTimer(const std::string& timer_id, zx::time timestamp,
                                    zx::duration timeout) {
  LogEvent(
      std::make_unique<EndTimerEvent>(timer_id, timestamp.get() / ZX_USEC(1), timeout.to_secs()));
}

void BaseCobaltLoggerImpl::LogIntHistogram(
    uint32_t metric_id, uint32_t event_code, const std::string& component,
    std::vector<fuchsia::cobalt::HistogramBucket> histogram) {
  LogEvent(std::make_unique<IntHistogramEvent>(
      metric_id, event_code, component,
      std::vector<fuchsia::cobalt::HistogramBucket>(std::move(histogram))));
}

void BaseCobaltLoggerImpl::LogCustomEvent(
    uint32_t metric_id, std::vector<fuchsia::cobalt::CustomEventValue> event_values) {
  LogEvent(std::make_unique<CustomEvent>(
      metric_id, std::vector<fuchsia::cobalt::CustomEventValue>(std::move(event_values))));
}

void BaseCobaltLoggerImpl::LogCobaltEvent(fuchsia::cobalt::CobaltEvent event) {
  LogEvent(std::make_unique<CobaltEvent>(std::move(event)));
}

void BaseCobaltLoggerImpl::LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events) {
  LogEvent(std::make_unique<CobaltEvents>(std::move(events)));
}

void BaseCobaltLoggerImpl::LogEvent(std::unique_ptr<BaseEvent> event) {
  if (dispatcher_ == async_get_default_dispatcher()) {
    LogEventOnMainThread(std::move(event));
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  async::PostTask(dispatcher_,
                  [event = std::move(event), this]() mutable { this->LogEvent(std::move(event)); });
}

void BaseCobaltLoggerImpl::ConnectToCobaltApplication() {
  logger_factory_ = ConnectToLoggerFactory();
  logger_factory_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "logger_factory_ experienced an error";
  });

  if (!logger_factory_) {
    return;
  }

  logger_factory_->CreateLoggerFromProjectId(project_id_, logger_.NewRequest(),
                                             CreateLoggerCallback("CreateLoggerFromProjectId"));
}

std::function<void(fuchsia::cobalt::Status)> BaseCobaltLoggerImpl::CreateLoggerCallback(
    const std::string& method_name) {
  return [this, method_name](Status status) {
    if (status == Status::OK) {
      if (logger_) {
        logger_ready_ = true;
        logger_.set_error_handler([this](zx_status_t status) { OnConnectionError(); });
        if (events_in_transit_.empty()) {
          SendEvents();
        }
      } else {
        OnConnectionError();
      }
    } else {
      FX_LOGST(ERROR, "cobalt_lib") << method_name << "() failed";
    }
    logger_factory_.Unbind();
  };
}

void BaseCobaltLoggerImpl::OnTransitFail() {
  // Ugly way to move unique_ptrs between sets
  for (const auto& event : events_in_transit_) {
    events_to_send_.insert(std::move(const_cast<std::unique_ptr<BaseEvent>&>(event)));
  }

  events_in_transit_.clear();
}

void BaseCobaltLoggerImpl::OnConnectionError() {
  FX_LOGS(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  OnTransitFail();
  logger_ready_ = false;
  logger_.Unbind();
  async::PostDelayedTask(
      dispatcher_, [this]() { ConnectToCobaltApplication(); }, backoff_.GetNext());
}

void BaseCobaltLoggerImpl::LogEventOnMainThread(std::unique_ptr<BaseEvent> event) {
  events_to_send_.insert(std::move(event));
  if (!logger_ready_ || !events_in_transit_.empty()) {
    return;
  }

  SendEvents();
}

void BaseCobaltLoggerImpl::SendEvents() {
  FX_DCHECK(events_in_transit_.empty());

  if (events_to_send_.empty()) {
    return;
  }

  events_in_transit_ = std::move(events_to_send_);
  events_to_send_.clear();

  auto complete_count = std::make_shared<int>(events_in_transit_.size());
  for (auto& event : events_in_transit_) {
    event->Log(&logger_, [this, event_ptr = event.get(), complete_count](Status status) {
      LogEventCallback(event_ptr, status);
      (*complete_count)--;

      // All events have been logged.
      if (!*complete_count) {
        // No transient errors.
        if (events_in_transit_.empty()) {
          backoff_.Reset();
          // Send any event received while |events_in_transit_| was not
          // empty.
          SendEvents();
          return;
        }

        // A transient error happened, retry after a delay.
        async::PostDelayedTask(
            dispatcher_,
            [this]() {
              OnTransitFail();
              SendEvents();
            },
            backoff_.GetNext());
      }
    });
  }
}

void BaseCobaltLoggerImpl::LogEventCallback(const BaseEvent* event, Status status) {
  switch (status) {
    case Status::INVALID_ARGUMENTS:
    case Status::SHUT_DOWN:
    case Status::EVENT_TOO_BIG:  // fall through
      // Log the failure.
      FX_LOGS(WARNING) << "Cobalt rejected event for metric: " << event->metric_id()
                       << " with status: " << fidl::ToUnderlying(status);
    case Status::OK:  // fall through
      // Remove the event from the set of events to send.
      events_in_transit_.erase(std::lower_bound(
          events_in_transit_.begin(), events_in_transit_.end(), event,
          [](const std::unique_ptr<BaseEvent>& a, const BaseEvent* b) { return a.get() < b; }));
      break;
    case Status::INTERNAL_ERROR:
    case Status::BUFFER_FULL:
      // Keep the event for re-queueing.
      break;
  }
}

fidl::InterfacePtr<LoggerFactory> CobaltLoggerImpl::ConnectToLoggerFactory() {
  return services_->Connect<LoggerFactory>();
}

CobaltLoggerImpl::CobaltLoggerImpl(async_dispatcher_t* dispatcher,
                                   std::shared_ptr<sys::ServiceDirectory> services,
                                   uint32_t project_id)
    : BaseCobaltLoggerImpl(dispatcher, project_id), services_(services) {
  ConnectToCobaltApplication();
}

}  // namespace cobalt
