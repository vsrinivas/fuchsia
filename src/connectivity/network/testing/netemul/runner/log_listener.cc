// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener.h"

#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

#include "log_listener_log_sink.h"
#include "log_listener_ostream.h"

namespace netemul {
namespace internal {

/*
 * LogListenerImpl
 *
 * public
 */

LogListenerImpl::LogListenerImpl(
    fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
    std::string prefix, async_dispatcher_t* dispatcher)
    : binding_(this, std::move(request), dispatcher),
      prefix_(std::move(prefix)),
      dropped_logs_(0) {
  binding_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "LogListenerImpl error: " << zx_status_get_string(status)
                   << std::endl;
  });
}

void LogListenerImpl::Log(fuchsia::logger::LogMessage m) {
  // Actually process the log
  LogImpl(std::move(m));
}

void LogListenerImpl::LogMany(std::vector<fuchsia::logger::LogMessage> ms) {
  for (auto& m : ms) {
    Log(std::move(m));
  }
}

void LogListenerImpl::Done() { return; }

}  // namespace internal

/*
 * LogListener
 *
 * public
 */

LogListener::LogListener(
    std::unique_ptr<fuchsia::logger::LogFilterOptions> log_filter_options,
    fidl::InterfaceHandle<fuchsia::logger::LogListener> loglistener_handle,
    std::shared_ptr<internal::LogListenerImpl> loglistener_impl)
    : log_filter_options_(std::move(log_filter_options)),
      loglistener_handle_(std::move(loglistener_handle)),
      loglistener_impl_(std::move(loglistener_impl)) {}

bool LogListener::Bindable() const { return loglistener_handle_.is_valid(); }

void LogListener::BindToLogService(
    fuchsia::netemul::environment::ManagedEnvironment* env) {
  // Connect to a remote that implements the fuchsia.logger.Log interface
  // within |env|.
  fuchsia::logger::LogPtr log_service;
  log_service.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "LogListenerImpl error: " << zx_status_get_string(status)
                   << std::endl;
  });

  env->ConnectToService(fuchsia::logger::Log::Name_,
                        log_service.NewRequest().TakeChannel());

  log_service->Listen(std::move(loglistener_handle_),
                      std::move(log_filter_options_));
}

std::shared_ptr<internal::LogListenerImpl> LogListener::GetLogListenerImpl()
    const {
  return loglistener_impl_;
}

std::unique_ptr<LogListener> LogListener::Create(
    fuchsia::netemul::environment::LoggerOptions logger_options,
    const std::string& prefix, async_dispatcher_t* dispatcher) {
  if (!logger_options.has_enabled() || !logger_options.enabled()) {
    return nullptr;
  }

  // Create an instance of the LogListener implementation
  // and start listening for logs
  fidl::InterfaceHandle<fuchsia::logger::LogListener> loglistener_h;
  std::shared_ptr<internal::LogListenerImpl> impl;

  if (logger_options.has_syslog_output() && logger_options.syslog_output()) {
    // Create a LogListenerImpl that forwards logs to another LogSink
    impl.reset(new internal::LogListenerLogSinkImpl(
        loglistener_h.NewRequest(), prefix, zx::socket(), dispatcher));
  } else {
    // Create a LogListenerImpl that writes logs to stdout.
    impl.reset(new internal::LogListenerOStreamImpl(
        loglistener_h.NewRequest(), prefix, &std::cout, dispatcher));
  }

  if (!impl) {
    FXL_LOG(ERROR) << "Failed to create a LogListenerImpl";
    return nullptr;
  }

  std::unique_ptr<fuchsia::logger::LogFilterOptions> log_filter_options;
  if (logger_options.has_filter_options()) {
    log_filter_options = std::make_unique<fuchsia::logger::LogFilterOptions>(
        logger_options.filter_options());
  }

  return std::make_unique<LogListener>(
      std::move(log_filter_options), std::move(loglistener_h), std::move(impl));
}

}  // namespace netemul
