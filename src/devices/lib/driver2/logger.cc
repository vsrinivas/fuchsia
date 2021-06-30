// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/logger.h"

#include <fuchsia/logger/llcpp/fidl.h>

zx::status<Logger> Logger::Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                  std::string_view name, fx_log_severity_t min_severity) {
  zx::socket client_end, server_end;
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &client_end, &server_end);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto ns_result = ns.Connect("/svc/fuchsia.logger.LogSink");
  if (ns_result.is_error()) {
    return ns_result.take_error();
  }

  fidl::Client<fuchsia_logger::LogSink> log_sink(std::move(*ns_result), dispatcher);
  auto sink_result = log_sink->Connect(std::move(server_end));
  if (!sink_result.ok()) {
    return zx::error(sink_result.status());
  }

  const char* tags[] = {name.data()};
  fx_logger_config_t config = {
      .min_severity = min_severity,
      .console_fd = -1,
      .log_service_channel = client_end.release(),
      .tags = tags,
      .num_tags = std::size(tags),
  };
  fx_logger_t* logger;
  status = fx_logger_create(&config, &logger);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(Logger(logger));
}

Logger::Logger(fx_logger_t* logger) : logger_(logger) {}

Logger::~Logger() {
  if (logger_ != nullptr) {
    fx_logger_destroy(logger_);
  }
}

Logger::Logger(Logger&& other) noexcept : logger_(other.logger_) { other.logger_ = nullptr; }

Logger& Logger::operator=(Logger&& other) noexcept {
  this->~Logger();
  logger_ = other.logger_;
  other.logger_ = nullptr;
  return *this;
}

void Logger::log(fx_log_severity_t severity, const char* file, int line, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  log(severity, file, line, msg, args);
  va_end(args);
}

void Logger::log(fx_log_severity_t severity, const char* file, int line, const char* msg,
                 va_list args) {
  fx_logger_logf_with_source(logger_, severity, nullptr, file, line, msg, args);
}
