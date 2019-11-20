// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/cobalt.h"

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::cobalt::LoggerFactory;
using fuchsia::cobalt::ReleaseStage;
using fuchsia::cobalt::Status;
using fxl::StringPrintf;

constexpr char kProjectName[] = "feedback";
constexpr ReleaseStage kReleaseStage = ReleaseStage::DOGFOOD;

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

std::string Cobalt::Event::ToString() const {
  return fxl::StringPrintf("(metric_id: %u, event_code: %u)", metric_id, event_code);
}

Cobalt::Cobalt(std::shared_ptr<sys::ServiceDirectory> services) : services_(services) {
  SetUpLogger();
}

void Cobalt::SetUpLogger() {
  // Set up the logger factory
  logger_factory_ = services_->Connect<LoggerFactory>();

  logger_factory_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Error with fuchsia.cobalt.LoggerFactory";
  });

  // We don't need a long standing connection to the LoggerFactory so we unbind afer setting up the
  // Logger.
  logger_factory_->CreateLoggerFromProjectName(
      kProjectName, kReleaseStage, logger_.NewRequest(), [this](Status status) {
        logger_factory_.Unbind();

        if (status != Status::OK) {
          FX_LOGS(ERROR) << "Failed to set up Cobalt: " << ToString(status);
          can_log_event_ = false;
          return;
        }

        logger_.set_error_handler([this](zx_status_t zx_status) {
          FX_PLOGS(ERROR, zx_status) << "Error with fuchsia.cobalt.Logger";
          can_log_event_ = false;
        });
        can_log_event_ = true;
        FlushPendingEvents();
      });
}

void Cobalt::Log(const Event& event) {
  logger_->LogEvent(event.metric_id, event.event_code, [=](Status status) {
    if (status != Status::OK) {
      FX_LOGS(INFO) << StringPrintf("Cobalt logging error: status %s, event %s",
                                    ToString(status).c_str(), event.ToString().c_str());
    }
  });
}

void Cobalt::FlushPendingEvents() {
  for (const auto& event : earliest_pending_events_) {
    if (!logger_) {
      FX_LOGS(ERROR) << "Logger no longer available, stopping flushing of queue";
      return;
    }
    Log(event);
    earliest_pending_events_.pop_front();
  }
}

void Cobalt::Log(uint32_t metric_id, uint32_t event_code) {
  Event event(metric_id, event_code);
  if (can_log_event_) {
    Log(event);
  } else if (earliest_pending_events_.size() < kMaxQueueSize) {
    earliest_pending_events_.push_back(event);
  } else {
    FX_LOGS(WARNING) << fxl::StringPrintf("Dropping Cobalt event %s - pending queue is full",
                                          event.ToString().c_str());
  }
}

}  // namespace feedback
