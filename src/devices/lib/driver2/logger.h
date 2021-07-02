// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_LOGGER_H_
#define SRC_DEVICES_LIB_DRIVER2_LOGGER_H_

#include <lib/syslog/logger.h>

#include "src/devices/lib/driver2/namespace.h"

#define FDF_LOGL(severity, logger, msg...) logger.log(FX_LOG_##severity, __FILE__, __LINE__, msg)
#define FDF_LOG(severity, msg...) FDF_LOGL(severity, logger_, msg)

namespace driver {

// Provides a driver's logger.
class Logger {
 public:
  // Creates a logger with a given `name`, which will only send logs that are of
  // at least `min_severity`.
  static zx::status<Logger> Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                   std::string_view name,
                                   fx_log_severity_t min_severity = FX_LOG_SEVERITY_DEFAULT);

  Logger() = default;
  ~Logger();

  Logger(Logger&& other) noexcept;
  Logger& operator=(Logger&& other) noexcept;

  void log(fx_log_severity_t severity, const char* file, int line, const char* msg, ...)
      __PRINTFLIKE(5, 6);
  void log(fx_log_severity_t severity, const char* file, int line, const char* msg, va_list args);

 private:
  explicit Logger(fx_logger_t* logger);

  Logger(const Logger& other) = delete;
  Logger& operator=(const Logger& other) = delete;

  fx_logger_t* logger_ = nullptr;
};

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_LOGGER_H_
