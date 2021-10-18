// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_LOGGER_H_
#define SRC_DEVICES_LIB_DRIVER2_LOGGER_H_

#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <lib/zx/socket.h>

#include "src/devices/lib/driver2/namespace.h"

#define FDF_LOGL(severity, logger, msg...) \
  logger.logf((FUCHSIA_LOG_##severity), nullptr, __FILE__, __LINE__, msg)
#define FDF_LOG(severity, msg...) FDF_LOGL(severity, logger_, msg)

namespace driver {

// Provides a driver's logger.
class Logger {
 public:
  // Creates a logger with a given `name`, which will only send logs that are of
  // at least `min_severity`.
  static zx::status<Logger> Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                   std::string_view name,
                                   FuchsiaLogSeverity min_severity = FUCHSIA_LOG_INFO);

  Logger() = default;
  ~Logger();

  Logger(Logger&& other) noexcept;
  Logger& operator=(Logger&& other) noexcept;

  void logf(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
            const char* msg, ...) __PRINTFLIKE(6, 7);
  void logvf(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
             const char* msg, va_list args);

  // Begins a structured logging record. You probably don't want to call
  // this directly.
  void BeginRecord(fuchsia_syslog::LogBuffer& buffer, FuchsiaLogSeverity severity,
                   cpp17::optional<cpp17::string_view> file_name, unsigned int line,
                   cpp17::optional<cpp17::string_view> message,
                   cpp17::optional<cpp17::string_view> condition, bool is_printf, uint32_t dropped);

  // Sends a log record to the backend. You probably don't want to call this directly.
  // This call also increments dropped_logs_, which is why we don't call FlushRecord
  // on LogBuffer directly.
  bool FlushRecord(fuchsia_syslog::LogBuffer& buffer, uint32_t dropped);

 private:
  Logger(const Logger& other) = delete;
  Logger& operator=(const Logger& other) = delete;

  // For thread-safety these members should be treated as read-only.
  // They are non-const only to allow move-assignment of Logger.
  std::string tag_;
  zx::socket socket_;
  // Messages below this won't be logged. This field is thread-safe.
  std::atomic<FuchsiaLogSeverity> severity_ = FUCHSIA_LOG_INFO;
  // Dropped log count. This is thread-safe and is reset on success.
  std::atomic<uint32_t> dropped_logs_;
};

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_LOGGER_H_
