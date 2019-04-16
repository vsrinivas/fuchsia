// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener.h"

#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

#include "format.h"

namespace netemul {

namespace internal {

/*
 * LogListenerImpl
 *
 * public
 */

LogListenerImpl::LogListenerImpl(
    fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
    std::string prefix, std::ostream* stream, bool klogs_enabled,
    async_dispatcher_t* dispatcher)
    : binding_(this, std::move(request), dispatcher),
      prefix_(std::move(prefix)),
      stream_(stream),
      klogs_enabled_(klogs_enabled) {
  binding_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "LogListenerImpl error: " << zx_status_get_string(status)
                   << std::endl;
  });
}

void LogListenerImpl::Log(fuchsia::logger::LogMessage m) {
  // TODO(ghanan): Filter out kernel logs before reaching here.
  // Ignore kernel logs
  if (!klogs_enabled_ &&
      std::find(m.tags.begin(), m.tags.end(), "klog") != m.tags.end()) {
    return;
  }

  *stream_ << "[" << prefix_ << "]";
  FormatTime(m.time);
  *stream_ << "[" << m.pid << "]"
           << "[" << m.tid << "]";
  FormatTags(m.tags);
  FormatLogLevel(m.severity);
  *stream_ << " " << m.msg << std::endl;
}

void LogListenerImpl::LogMany(std::vector<fuchsia::logger::LogMessage> ms) {
  for (auto& m : ms) {
    Log(std::move(m));
  }
}

void LogListenerImpl::Done() {
  FXL_LOG(INFO) << "DONE";
  return;
}

/*
 * LogListenerImpl
 *
 * private
 */

void LogListenerImpl::FormatTime(const zx_time_t timestamp) {
  internal::FormatTime(stream_, timestamp);
}

void LogListenerImpl::FormatTags(const std::vector<std::string>& tags) {
  auto it = tags.begin();

  *stream_ << "[";

  while (it != tags.end()) {
    *stream_ << *it;

    it = std::next(it);

    if (it != tags.end()) {
      *stream_ << ",";
    }
  }

  *stream_ << "]";
}

void LogListenerImpl::FormatLogLevel(const int32_t severity) {
  switch (severity) {
    case 0:
      *stream_ << "[INFO]";
      break;

    case 1:
      *stream_ << "[WARNING]";
      break;

    case 2:
      *stream_ << "[ERROR]";
      break;

    case 3:
      *stream_ << "[FATAL]";
      break;

    default:
      if (severity > 3) {
        *stream_ << "[INVALID]";
      } else {
        *stream_ << "[VLOG(" << severity << ")]";
      }
  }
}

}  // namespace internal

/*
 * LogListener
 *
 * public
 */

LogListener::LogListener(std::unique_ptr<internal::LogListenerImpl> impl)
    : loglistener_impl_(std::move(impl)) {}

std::unique_ptr<LogListener> LogListener::Create(
    fuchsia::netemul::environment::ManagedEnvironment* env,
    const fuchsia::netemul::environment::LoggerOptions& logger_options,
    const std::string& prefix, async_dispatcher_t* dispatcher) {
  if (!logger_options.has_enabled() || !logger_options.enabled()) {
    return nullptr;
  }

  // Create the client side interface and connect to a remote
  // that implementes the fuchsia.logger.Log interface within
  // `env`.
  fuchsia::logger::LogPtr log_service;
  log_service.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "LogListenerImpl error: " << zx_status_get_string(status)
                   << std::endl;
  });

  env->ConnectToService(fuchsia::logger::Log::Name_,
                        log_service.NewRequest().TakeChannel());

  bool klogs_enabled =
      logger_options.has_klogs_enabled() && logger_options.klogs_enabled();

  // Create an instance of the LogListener implementation
  // and start listening for logs
  fidl::InterfaceHandle<fuchsia::logger::LogListener> loglistener_h;
  std::unique_ptr<internal::LogListenerImpl> impl(
      new internal::LogListenerImpl(loglistener_h.NewRequest(), prefix,
                                    &std::cout, klogs_enabled, dispatcher));

  if (logger_options.has_filter_options()) {
    log_service->Listen(std::move(loglistener_h),
                        std::make_unique<fuchsia::logger::LogFilterOptions>(
                            logger_options.filter_options()));
  } else {
    log_service->Listen(std::move(loglistener_h), nullptr);
  }

  return std::make_unique<LogListener>(std::move(impl));
}

}  // namespace netemul
