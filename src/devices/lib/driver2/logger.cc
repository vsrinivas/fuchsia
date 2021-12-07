// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/logger.h"

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/fdio/directory.h>

#include <cstdarg>

namespace flog = fuchsia_syslog;

namespace driver {

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

bool Logger::FlushRecord(flog::LogBuffer& buffer, uint32_t dropped) {
  if (!buffer.FlushRecord()) {
    dropped_logs_.fetch_add(dropped, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void Logger::BeginRecord(flog::LogBuffer& buffer, FuchsiaLogSeverity severity,
                         cpp17::optional<cpp17::string_view> file_name, unsigned int line,
                         cpp17::optional<cpp17::string_view> message,
                         cpp17::optional<cpp17::string_view> condition, bool is_printf,
                         uint32_t dropped) {
  static zx_koid_t pid = GetKoid(zx_process_self());
  static thread_local zx_koid_t tid = GetKoid(zx_thread_self());
  buffer.BeginRecord(severity, file_name, line, message, condition, is_printf, socket_.borrow(),
                     dropped, pid, tid);
}

zx::status<Logger> Logger::Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                  std::string_view name, FuchsiaLogSeverity min_severity) {
  zx::socket client_end, server_end;
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &client_end, &server_end);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto ns_result = ns.Connect<fuchsia_logger::LogSink>();
  if (ns_result.is_error()) {
    return ns_result.take_error();
  }

  fidl::WireSharedClient<fuchsia_logger::LogSink> log_sink(std::move(*ns_result), dispatcher);
  auto sink_result = log_sink->ConnectStructured(std::move(server_end));
  if (!sink_result.ok()) {
    return zx::error(sink_result.status());
  }

  Logger logger;
  logger.dropped_logs_ = 0;
  logger.tag_ = name;
  logger.severity_ = min_severity;
  logger.socket_ = std::move(client_end);
  return zx::ok(std::move(logger));
}

Logger::~Logger() = default;

Logger::Logger(Logger&& other) noexcept {
  dropped_logs_.store(other.dropped_logs_);
  severity_.store(other.severity_);
  socket_ = std::move(other.socket_);
  tag_ = std::move(other.tag_);
}

Logger& Logger::operator=(Logger&& other) noexcept {
  dropped_logs_.store(other.dropped_logs_);
  severity_.store(other.severity_);
  socket_ = std::move(other.socket_);
  tag_ = std::move(other.tag_);
  return *this;
}

uint32_t Logger::GetAndResetDropped() {
  return dropped_logs_.exchange(0, std::memory_order_relaxed);
}

FuchsiaLogSeverity Logger::GetSeverity() { return severity_.load(std::memory_order_relaxed); }

void Logger::logf(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
                  const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  logvf(severity, tag, file, line, msg, args);
  va_end(args);
}

namespace {
const char* StripDots(const char* path) {
  while (strncmp(path, "../", 3) == 0) {
    path += 3;
  }
  return path;
}

const char* StripPath(const char* path) {
  auto p = strrchr(path, '/');
  if (p) {
    return p + 1;
  } else {
    return path;
  }
}
}  // namespace

static const char* StripFile(const char* file, FuchsiaLogSeverity severity) {
  return severity > FUCHSIA_LOG_INFO ? StripDots(file) : StripPath(file);
}

void Logger::logvf(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
                   const char* msg, va_list args) {
  if (!file || line <= 0) {
    // We require a file and line number for printf-style logs.
    return;
  }
  if (severity < severity_.load()) {
    return;
  }
  uint32_t dropped = dropped_logs_.exchange(0, std::memory_order_relaxed);
  constexpr size_t kFormatStringLength = 1024;
  char fmt_string[kFormatStringLength];
  fmt_string[kFormatStringLength - 1] = 0;
  int n = kFormatStringLength;
  // Format
  // Number of bytes written not including null terminator
  int count = 0;
  count = vsnprintf(fmt_string, n, msg, args) + 1;
  if (count < 0) {
    // Invalid arguments -- we don't support logging empty strings
    // for legacy printf-style messages.
    return;
  }

  if (count >= n) {
    // truncated
    constexpr char kEllipsis[] = "...";
    constexpr size_t kEllipsisSize = sizeof(kEllipsis);
    snprintf(fmt_string + kFormatStringLength - 1 - kEllipsisSize, kEllipsisSize, kEllipsis);
  }

  // TODO(fxbug.dev/72675): Pass file/line info regardless of severity in all cases.
  // This is currently only enabled for drivers.
  file = StripFile(file, severity);
  flog::LogBuffer buffer;
  BeginRecord(buffer, severity, file, line, fmt_string, std::nullopt, this->socket_.get(), dropped);
  buffer.WriteKeyValue("tag", tag_);
  if (tag) {
    buffer.WriteKeyValue("tag", tag);
  }
  FlushRecord(buffer, dropped);
}

}  // namespace driver
