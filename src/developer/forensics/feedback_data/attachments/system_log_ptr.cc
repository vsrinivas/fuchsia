// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/system_log_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cinttypes>
#include <string>
#include <vector>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/log_format.h"

namespace forensics {
namespace feedback_data {

::fit::promise<AttachmentValue> CollectSystemLog(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 fit::Timeout timeout) {
  std::unique_ptr<LogListener> log_listener = std::make_unique<LogListener>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto logs = log_listener->CollectLogs(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(logs),
                                              /*args=*/std::move(log_listener));
}

LogListener::LogListener(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services)
    : binding_(this), logger_(dispatcher, services) {}

::fit::promise<AttachmentValue> LogListener::CollectLogs(fit::Timeout timeout) {
  ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener_h;
  binding_.Bind(log_listener_h.NewRequest());
  binding_.set_error_handler([this](zx_status_t status) {
    if (logger_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection with fuchsia.logger.LogListenerSafe";
    logger_.CompleteError(Error::kConnectionError);
  });

  logger_->DumpLogsSafe(std::move(log_listener_h), /*options=*/nullptr);

  return logger_.WaitForDone(std::move(timeout))
      .then([this](::fit::result<void, Error>& result) -> ::fit::result<AttachmentValue> {
        binding_.Close(ZX_OK);

        if (log_messages_.empty()) {
          FX_LOGS(WARNING) << "Empty system log";
          AttachmentValue value = (result.is_ok()) ? AttachmentValue(Error::kMissingValue)
                                                   : AttachmentValue(result.error());
          return ::fit::ok(std::move(value));
        }

        std::string logs;
        for (const auto& message : log_messages_) {
          logs += Format(message);
        }

        AttachmentValue value = (result.is_ok()) ? AttachmentValue(std::move(logs))
                                                 : AttachmentValue(std::move(logs), result.error());

        return ::fit::ok(std::move(value));
      });
}

void LogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> messages,
                          LogManyCallback done) {
  for (auto& message : messages) {
    Log(std::move(message), []() {});
  }
  done();
}

void LogListener::Log(fuchsia::logger::LogMessage message, LogCallback done) {
  // Keep |log_messages_| sorted by assuming |log_messages_| is already sorted and inserting
  // |message| after the last message with a timestamp less than or equal to |message.time|. This is
  // done because log messages are received mostly in order and messages with the same timestamp
  // should not be reordered.
  auto it = log_messages_.crbegin();
  while (it != log_messages_.crend() && message.time < it->time) {
    ++it;
  }
  log_messages_.insert(it.base(), std::move(message));
  done();
}

void LogListener::Done() {
  if (logger_.IsAlreadyDone()) {
    return;
  }

  logger_.CompleteOk();
}

}  // namespace feedback_data
}  // namespace forensics
