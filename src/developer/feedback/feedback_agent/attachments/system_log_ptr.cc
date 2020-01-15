// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/system_log_ptr.h"

#include <fuchsia/mem/cpp/fidl.h>
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
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<fuchsia::mem::Buffer> CollectSystemLog(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    zx::duration timeout,
                                                    std::shared_ptr<Cobalt> cobalt) {
  std::unique_ptr<LogListener> log_listener =
      std::make_unique<LogListener>(dispatcher, services, std::move(cobalt));

  return log_listener->CollectLogs(timeout).then(
      [log_listener = std::move(log_listener)](
          const fit::result<void>& result) -> fit::result<fuchsia::mem::Buffer> {
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

LogListener::LogListener(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services,
                         std::shared_ptr<Cobalt> cobalt)
    : dispatcher_(dispatcher), services_(services), cobalt_(std::move(cobalt)), binding_(this) {}

fit::promise<void> LogListener::CollectLogs(zx::duration timeout) {
  FXL_CHECK(!has_called_collect_logs_) << "CollectLogs() is not intended to be called twice";
  has_called_collect_logs_ = true;

  fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener_h;
  binding_.Bind(log_listener_h.NewRequest());
  binding_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "LogListener error";
    done_.completer.complete_error();
  });

  logger_ = services_->Connect<fuchsia::logger::Log>();
  logger_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to Log service";
    done_.completer.complete_error();
  });
  // Resets |log_many_called_| for the new call to DumpLogs().
  log_many_called_ = false;
  logger_->DumpLogs(std::move(log_listener_h), /*options=*/nullptr);

  // fit::promise does not have the notion of a timeout. So we post a delayed task that will call
  // the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when the fit::bridge is
  // completed by Done() or another error.
  //
  // It is safe to pass "this" to the fit::function as the callback won't be callable when the
  // CancelableClosure goes out of scope, which is before "this".
  done_after_timeout_.Reset([this] {
    if (!done_.completer) {
      return;
    }

    FX_LOGS(ERROR) << "System log collection timed out";
    cobalt_->LogOccurrence(TimedOutData::kSystemLog);
    done_.completer.complete_error();
  });
  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed task, no timeout for log collection";
  }

  return done_.consumer.promise_or(fit::error()).then([this](fit::result<void>& result) {
    done_after_timeout_.Cancel();
    binding_.Unbind();
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

namespace {

std::string SeverityToString(const int32_t severity) {
  if (severity < 0) {
    return fxl::StringPrintf("VLOG(%d)", -severity);
  } else if (severity == FX_LOG_INFO) {
    return "INFO";
  } else if (severity == FX_LOG_WARNING) {
    return "WARN";
  } else if (severity == FX_LOG_ERROR) {
    return "ERROR";
  } else if (severity == FX_LOG_FATAL) {
    return "FATAL";
  }
  return "INVALID";
}

}  // namespace

void LogListener::Log(fuchsia::logger::LogMessage message) {
  logs_ += fxl::StringPrintf("[%05d.%03d][%05" PRIu64 "][%05" PRIu64 "][%s] %s: %s\n",
                             static_cast<int>(message.time / 1000000000ULL),
                             static_cast<int>((message.time / 1000000ULL) % 1000ULL), message.pid,
                             message.tid, fxl::JoinStrings(message.tags, ", ").c_str(),
                             SeverityToString(message.severity).c_str(), message.msg.c_str());
}

void LogListener::Done() {
  if (!done_.completer) {
    return;
  }

  if (!log_many_called_) {
    FX_LOGS(WARNING) << "Done() was called before any calls to LogMany()";
  }

  if (logs_.empty()) {
    FX_LOGS(WARNING) << "Done() was called, but no logs have been collected yet";
  }

  done_.completer.complete_ok();
}

}  // namespace feedback
