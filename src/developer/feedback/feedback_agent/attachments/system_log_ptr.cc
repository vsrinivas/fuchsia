// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/system_log_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cinttypes>
#include <string>
#include <vector>

#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/developer/feedback/utils/log_format.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<AttachmentValue> CollectSystemLog(async_dispatcher_t* dispatcher,
                                               std::shared_ptr<sys::ServiceDirectory> services,
                                               zx::duration timeout, Cobalt* cobalt) {
  std::unique_ptr<LogListener> log_listener =
      std::make_unique<LogListener>(dispatcher, services, cobalt);

  return log_listener->CollectLogs(timeout).then(
      [log_listener = std::move(log_listener)](
          const fit::result<void>& result) -> fit::result<AttachmentValue> {
        if (!result.is_ok()) {
          FX_LOGS(WARNING) << "System log collection was interrupted - "
                              "logs may be partial or missing";
        }

        const std::string logs = log_listener->CurrentLogs();
        if (logs.empty()) {
          FX_LOGS(WARNING) << "Empty system log";
          return fit::error();
        }

        return fit::ok(logs);
      });
}

LogListener::LogListener(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt)
    : services_(services),
      binding_(this),
      cobalt_(cobalt),
      bridge_(dispatcher, "System log collection") {}

fit::promise<void> LogListener::CollectLogs(zx::duration timeout) {
  FXL_CHECK(!has_called_collect_logs_) << "CollectLogs() is not intended to be called twice";
  has_called_collect_logs_ = true;

  fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener_h;
  binding_.Bind(log_listener_h.NewRequest());
  binding_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "LogListener error";
    bridge_.CompleteError();
  });

  logger_ = services_->Connect<fuchsia::logger::Log>();
  logger_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to Log service";
    bridge_.CompleteError();
  });
  // Resets |log_many_called_| for the new call to DumpLogs().
  log_many_called_ = false;
  logger_->DumpLogs(std::move(log_listener_h), /*options=*/nullptr);

  return bridge_
      .WaitForDone(timeout,
                   /*if_timeout=*/[this] { cobalt_->LogOccurrence(TimedOutData::kSystemLog); })
      .then([this](fit::result<void>& result) {
        binding_.Close(ZX_OK);
        return std::move(result);
      });
}

void LogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> messages) {
  log_many_called_ = true;

  if (messages.empty()) {
    FX_LOGS(WARNING) << "LogMany() was called with no messages";
    return;
  }

  for (auto& message : messages) {
    Log(std::move(message));
  }
}

void LogListener::Log(fuchsia::logger::LogMessage message) { logs_ += Format(message); }

void LogListener::Done() {
  if (bridge_.IsAlreadyDone()) {
    return;
  }

  if (!log_many_called_) {
    FX_LOGS(WARNING) << "Done() was called before any calls to LogMany()";
  }

  if (logs_.empty()) {
    FX_LOGS(WARNING) << "Done() was called, but no logs have been collected yet";
  }

  bridge_.CompleteOk();
}

}  // namespace feedback
