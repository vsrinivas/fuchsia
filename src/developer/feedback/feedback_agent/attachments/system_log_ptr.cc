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

#include "src/developer/feedback/utils/log_format.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

::fit::promise<AttachmentValue> CollectSystemLog(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 fit::Timeout timeout) {
  std::unique_ptr<LogListener> log_listener = std::make_unique<LogListener>(dispatcher, services);

  return log_listener->CollectLogs(std::move(timeout))
      .then([log_listener = std::move(log_listener)](
                const ::fit::result<void>& result) -> ::fit::result<AttachmentValue> {
        if (!result.is_ok()) {
          FX_LOGS(WARNING) << "System log collection was interrupted - "
                              "logs may be partial or missing";
        }

        const std::string logs = log_listener->CurrentLogs();
        if (logs.empty()) {
          FX_LOGS(WARNING) << "Empty system log";
          return ::fit::error();
        }

        return ::fit::ok(logs);
      });
}

LogListener::LogListener(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services)
    : binding_(this), logger_(dispatcher, services) {}

::fit::promise<void> LogListener::CollectLogs(fit::Timeout timeout) {
  ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener_h;
  binding_.Bind(log_listener_h.NewRequest());
  binding_.set_error_handler([this](zx_status_t status) {
    if (logger_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "LogListenerSafe error";
    logger_.CompleteError();
  });

  // Resets |log_many_called_| for the new call to DumpLogs().
  log_many_called_ = false;
  logger_->DumpLogsSafe(std::move(log_listener_h), /*options=*/nullptr);

  return logger_.WaitForDone(std::move(timeout)).then([this](::fit::result<void>& result) {
    binding_.Close(ZX_OK);
    return std::move(result);
  });
}

void LogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> messages,
                          LogManyCallback done) {
  log_many_called_ = true;

  if (messages.empty()) {
    FX_LOGS(WARNING) << "LogMany() was called with no messages";
    return;
  }

  for (auto& message : messages) {
    Log(std::move(message), []() {});
  }
  done();
}

void LogListener::Log(fuchsia::logger::LogMessage message, LogCallback done) {
  logs_ += Format(message);
  done();
}

void LogListener::Done() {
  if (logger_.IsAlreadyDone()) {
    return;
  }

  if (!log_many_called_) {
    FX_LOGS(WARNING) << "Done() was called before any calls to LogMany()";
  }

  if (logs_.empty()) {
    FX_LOGS(WARNING) << "Done() was called, but no logs have been collected yet";
  }

  logger_.CompleteOk();
}

}  // namespace feedback
