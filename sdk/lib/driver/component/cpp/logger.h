// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_LOGGER_H_
#define LIB_DRIVER_COMPONENT_CPP_LOGGER_H_

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/driver/component/cpp/namespace.h>
#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <lib/zx/socket.h>

#define FDF_LOGL(severity, logger, msg...) \
  (logger).logf((FUCHSIA_LOG_##severity), nullptr, __FILE__, __LINE__, msg)
#define FDF_LOG(severity, msg...) FDF_LOGL(severity, *logger_, msg)

namespace driver {

// Provides a driver's logger.
class Logger {
 public:
  // Creates a logger with a given |name|, which will only send logs that are of
  // at least |min_severity|.
  // |dispatcher| must be single threaded or synchornized. Create must be called from the context of
  // the |dispatcher|.
  // If |wait_for_initial_interest| is true we this will synchronously query the
  // fuchsia.logger/LogSink for the min severity it should expect, overriding the min_severity
  // supplied.
  static zx::result<std::unique_ptr<Logger>> Create(
      const Namespace& ns, async_dispatcher_t* dispatcher, std::string_view name,
      FuchsiaLogSeverity min_severity = FUCHSIA_LOG_INFO, bool wait_for_initial_interest = true);

  Logger(std::string_view name, FuchsiaLogSeverity min_severity, zx::socket socket,
         fidl::WireClient<fuchsia_logger::LogSink> log_sink)
      : tag_(name),
        socket_(std::move(socket)),
        default_severity_(min_severity),
        severity_(min_severity),
        log_sink_(std::move(log_sink)) {}
  ~Logger();

  // Retrieves the number of dropped logs and resets it
  uint32_t GetAndResetDropped();

  FuchsiaLogSeverity GetSeverity();

  void SetSeverity(FuchsiaLogSeverity severity);

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

  void HandleInterest(fuchsia_diagnostics::wire::Interest interest);
  void OnInterestChange(
      fidl::WireUnownedResult<fuchsia_logger::LogSink::WaitForInterestChange>& result);

  // For thread-safety these members should be read-only.
  const std::string tag_;
  const zx::socket socket_;
  const FuchsiaLogSeverity default_severity_;
  // Messages below this won't be logged. This field is thread-safe.
  std::atomic<FuchsiaLogSeverity> severity_;
  // Dropped log count. This is thread-safe and is reset on success.
  std::atomic<uint32_t> dropped_logs_ = 0;

  // Used to learn about changes in severity.
  fidl::WireClient<fuchsia_logger::LogSink> log_sink_;
};

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_LOGGER_H_
