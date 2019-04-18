// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/log_listener.h"

#include <string>
#include <vector>

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fuchsia {
namespace feedback {
namespace {

const zx::duration kSystemLogCollectionTimeout = zx::sec(10);

}  // namespace

std::optional<fuchsia::mem::Buffer> CollectSystemLog(
    std::shared_ptr<::sys::ServiceDirectory> services) {
  std::unique_ptr<LogListener> log_listener;

  // We spawn a second loop to be able to wait on LogListener::Done().
  // We need the second loop because CollectSystemLog() presents a sync
  // interface, but LogListener is an inherently async interface.
  // We use the second loop to wait on the async API without blocking the thread
  // as LogListener runs on the same thread.
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  async::PostTask(loop.dispatcher(), [&loop, &log_listener, &services] {
    fuchsia::logger::LogPtr logger = services->Connect<fuchsia::logger::Log>();
    logger.set_error_handler([&loop](zx_status_t status) {
      FX_LOGS(ERROR) << "Lost connection to Log service: " << status << " ("
                     << zx_status_get_string(status) << ")";
      loop.Quit();
    });

    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener_h;
    log_listener = std::make_unique<LogListener>(log_listener_h.NewRequest(),
                                                 loop.dispatcher(),
                                                 [&loop] { loop.Quit(); });
    logger->DumpLogs(std::move(log_listener_h), /*options=*/nullptr);
  });
  if (loop.Run(zx::deadline_after(kSystemLogCollectionTimeout)) ==
      ZX_ERR_TIMED_OUT) {
    FX_LOGS(WARNING) << "System log collection timed out - logs may be partial";
  }

  const std::string logs = log_listener->CurrentLogs();
  log_listener.reset();
  loop.Shutdown();

  if (logs.empty()) {
    FX_LOGS(WARNING) << "Empty system log";
    return std::nullopt;
  }

  fsl::SizedVmo vmo;
  if (!fsl::VmoFromString(logs, &vmo)) {
    FX_LOGS(ERROR) << "Failed to convert system log string to vmo";
    return std::nullopt;
  }
  return std::move(vmo).ToTransport();
}

LogListener::LogListener(
    fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
    async_dispatcher_t* dispatcher, fit::function<void()> done)
    : binding_(this, std::move(request), dispatcher), done_(std::move(done)) {
  binding_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "LogListener error: " << status << " ("
                   << zx_status_get_string(status) << ")";
  });
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

void LogListener::Done() { done_(); }

}  // namespace feedback
}  // namespace fuchsia
