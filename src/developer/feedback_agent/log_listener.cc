// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/log_listener.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <string>
#include <vector>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fuchsia {
namespace feedback {
namespace {

const zx::duration kSystemLogCollectionTimeout = zx::sec(10);

}  // namespace

fit::promise<fuchsia::mem::Buffer> CollectSystemLog(
    std::shared_ptr<::sys::ServiceDirectory> services) {
  std::unique_ptr<LogListener> log_listener =
      std::make_unique<LogListener>(services);

  return log_listener->CollectLogs().then(
      [log_listener = std::move(log_listener)](const fit::result<void>& result)
          -> fit::result<fuchsia::mem::Buffer> {
        if (!result.is_ok()) {
          FX_LOGS(WARNING) << "System log collection was interrupted - "
                              "logs may be partial or missing";
        }

        const std::string logs = log_listener->CurrentLogs();
        if (logs.empty()) {
          FX_LOGS(WARNING) << "Empty system log";
          return fit::error();
        }

        fsl::SizedVmo vmo;
        if (!fsl::VmoFromString(logs, &vmo)) {
          FX_LOGS(ERROR) << "Failed to convert system log string to vmo";
          return fit::error();
        }
        return fit::ok(std::move(vmo).ToTransport());
      });
}

LogListener::LogListener(std::shared_ptr<::sys::ServiceDirectory> services)
    : services_(services), binding_(this) {}

fit::promise<void> LogListener::CollectLogs() {
  fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener_h;
  binding_.Bind(log_listener_h.NewRequest());
  binding_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "LogListener error: " << status << " ("
                   << zx_status_get_string(status) << ")";
    done_.completer.complete_error();
  });

  fuchsia::logger::LogPtr logger = services_->Connect<fuchsia::logger::Log>();
  logger.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Lost connection to Log service: " << status << " ("
                   << zx_status_get_string(status) << ")";
    done_.completer.complete_error();
  });
  logger->DumpLogs(std::move(log_listener_h), /*options=*/nullptr);

  // fit::promise does not have the notion of a timeout. So we post a delayed
  // task that will call the completer after the timeout and return an error.
  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [this] {
        // Check that the fit::bridge was not already completed by Done() or
        // another error.
        if (done_.completer) {
          FX_LOGS(ERROR) << "System log collection timed out";
          done_.completer.complete_error();
        }
      },
      kSystemLogCollectionTimeout);

  return done_.consumer.promise_or(fit::error());
}

void LogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> messages) {
  for (auto& message : messages) {
    Log(std::move(message));
  }
}

namespace {

std::string SeverityToString(const int32_t severity) {
  if (severity < 0) {
    return fxl::StringPrintf("VLOG(%d)", -severity);
  } else if (severity == 0) {
    return "INFO";
  } else if (severity == 1) {
    return "WARN";
  } else if (severity == 2) {
    return "ERROR";
  } else if (severity == 3) {
    return "FATAL";
  }
  return "INVALID";
}

}  // namespace

void LogListener::Log(fuchsia::logger::LogMessage message) {
  logs_ += fxl::StringPrintf(
      "[%05d.%03d][%05" PRIu64 "][%05" PRIu64 "][%s] %s: %s\n",
      static_cast<int>(message.time / 1000000000ULL),
      static_cast<int>((message.time / 1000000ULL) % 1000ULL), message.pid,
      message.tid, fxl::JoinStrings(message.tags, ", ").c_str(),
      SeverityToString(message.severity).c_str(), message.msg.c_str());
}

void LogListener::Done() { done_.completer.complete_ok(); }

}  // namespace feedback
}  // namespace fuchsia
