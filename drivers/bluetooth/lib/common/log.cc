// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log.h"

#include <stdarg.h>
#include <algorithm>

#include <ddk/debug.h>
#include <syslog/global.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "optional.h"

namespace btlib {
namespace common {
namespace {

std::atomic_bool g_use_fx_log(false);

uint32_t kDdkSeverities[kNumLogSeverities] = {
    DDK_LOG_ERROR, DDK_LOG_WARN, DDK_LOG_INFO, DDK_LOG_TRACE, DDK_LOG_SPEW,
};

fx_log_severity_t kSyslogSeverities[kNumLogSeverities] = {
    FX_LOG_ERROR, FX_LOG_WARNING, FX_LOG_INFO, -1, -2,
};

const char* const kLogSeverityNames[kNumLogSeverities] = {
    "ERROR", "WARN", "INFO", "TRACE", "SPEW",
};

constexpr size_t LogSeverityToIndex(LogSeverity severity) {
  return std::min(kNumLogSeverities - 1, static_cast<size_t>(severity));
}

inline uint32_t LogSeverityToDdkLog(LogSeverity severity) {
  return kDdkSeverities[LogSeverityToIndex(severity)];
}

inline fx_log_severity_t LogSeverityToFxLog(LogSeverity severity) {
  return kSyslogSeverities[LogSeverityToIndex(severity)];
}

inline const char* LogSeverityToString(LogSeverity severity) {
  return kLogSeverityNames[LogSeverityToIndex(severity)];
}

const char* StripPath(const char* path) {
  const char* p = strrchr(path, '/');
  return p ? p + 1 : path;
}

}  // namespace

bool IsLogLevelEnabled(LogSeverity severity) {
  if (g_use_fx_log) {
    fx_logger_t* logger = fx_log_get_logger();
    return logger &&
           (fx_logger_get_min_severity(logger) <= LogSeverityToFxLog(severity));
  }
  return zxlog_level_enabled_etc(LogSeverityToDdkLog(severity));
}

void LogMessage(const char* file, int line, LogSeverity severity,
                const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string msg = fxl::StringVPrintf(fmt, args);
  va_end(args);

  if (g_use_fx_log) {
    fx_logger_logf(fx_log_get_logger(), LogSeverityToFxLog(severity), tag,
                   "[%s(%d)]: %s", StripPath(file), line, msg.c_str());
  } else {
    driver_printf(LogSeverityToDdkLog(severity), "[%s - %s(%d)] %s: %s\n", tag,
                  StripPath(file), line, LogSeverityToString(severity),
                  msg.c_str());
  }
}

void UseSyslog() { g_use_fx_log = true; }

}  // namespace common
}  // namespace btlib
