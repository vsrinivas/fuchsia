// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_LOGGER_H_
#define SRC_DEVICES_LIB_DRIVER2_LOGGER_H_

#include <lib/syslog/logger.h>
#include <stdarg.h>

#include "namespace.h"

// Provides a driver's logger.
class Logger {
 public:
  // Creates a logger with a given |name|, which will only send logs that are of
  // at least |min_severity|.
  static zx::status<Logger> Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                   std::string_view name,
                                   fx_log_severity_t min_severity = FX_LOG_SEVERITY_DEFAULT);

  Logger() = default;
  ~Logger();

  Logger(Logger&& other) noexcept;
  Logger& operator=(Logger&& other) noexcept;

  // Sends a trace log.
  void trace(const char* msg, ...);
  // Sends a debug log.
  void debug(const char* msg, ...);
  // Sends an info log.
  void info(const char* msg, ...);
  // Sends a warning log.
  void warning(const char* msg, ...);
  // Sends an error log.
  void error(const char* msg, ...);

 private:
  explicit Logger(fx_logger_t* logger);

  Logger(const Logger& other) = delete;
  Logger& operator=(const Logger& other) = delete;

  void log(fx_log_severity_t severity, const char* msg, va_list args);

  fx_logger_t* logger_ = nullptr;
};

#endif  // SRC_DEVICES_LIB_DRIVER2_LOGGER_H_
