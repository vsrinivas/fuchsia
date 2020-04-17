// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/logging.h"

#include <algorithm>
#include <iostream>

#include "src/lib/fxl/build_config.h"
#include "src/lib/fxl/debug/debugger.h"
#include "src/lib/fxl/log_settings.h"

#if defined(__Fuchsia__)
#include <lib/syslog/global.h>
#include <zircon/status.h>
#elif defined(OS_ANDROID)
#include <android/log.h>
#elif defined(OS_IOS)
#include <lib/syslog.h>
#endif

namespace fxl {
namespace {

#ifndef __Fuchsia__
const char* const kLogSeverityNames[LOG_NUM_SEVERITIES] = {"INFO", "WARNING", "ERROR", "FATAL"};

const char* GetNameForLogSeverity(LogSeverity severity) {
  if (severity >= LOG_INFO && severity < LOG_NUM_SEVERITIES)
    return kLogSeverityNames[severity];
  return "UNKNOWN";
}
#endif

const char* StripDots(const char* path) {
  while (strncmp(path, "../", 3) == 0)
    path += 3;
  return path;
}

const char* StripPath(const char* path) {
  auto p = strrchr(path, '/');
  if (p)
    return p + 1;
  else
    return path;
}

}  // namespace

LogMessage::LogMessage(LogSeverity severity, const char* file, int line, const char* condition
#if defined(__Fuchsia__)
                       ,
                       zx_status_t status
#endif
                       )
    : severity_(severity),
      file_(file),
      line_(line)
#if defined(__Fuchsia__)
      ,
      status_(status)
#endif
{
  stream_ << "[";
  // With syslog the severity is included in the metadata so no need to add it
  // to the log message itself.
#ifndef __Fuchsia__
  if (severity >= LOG_INFO)
    stream_ << GetNameForLogSeverity(severity);
  else
    stream_ << "VERBOSE" << -severity;
  stream_ << ":";
#endif
  stream_ << (severity > LOG_INFO ? StripDots(file_) : StripPath(file_)) << "(" << line_ << ")] ";

  if (condition)
    stream_ << "Check failed: " << condition << ". ";
}

LogMessage::~LogMessage() {
#if defined(__Fuchsia__)
  if (status_ != std::numeric_limits<zx_status_t>::max()) {
    stream_ << ": " << status_ << " (" << zx_status_get_string(status_) << ")";
  }
#else
  stream_ << std::endl;
#endif

#if defined(__Fuchsia__)
  // Write fatal logs to stderr as well because death tests sometimes verify a certain
  // log message was printed prior to the crash.
  // TODO(samans): Convert tests to not depend on stderr. https://fxbug.dev/49593
  if (severity_ == LOG_FATAL)
    std::cerr << stream_.str() << std::endl;
  fx_logger_t* logger = fx_log_get_logger();
  fx_logger_log(logger, severity_, nullptr, stream_.str().c_str());
#elif defined(OS_ANDROID)
  android_LogPriority priority = (severity_ < 0) ? ANDROID_LOG_VERBOSE : ANDROID_LOG_UNKNOWN;
  switch (severity_) {
    case LOG_INFO:
      priority = ANDROID_LOG_INFO;
      break;
    case LOG_WARNING:
      priority = ANDROID_LOG_WARN;
      break;
    case LOG_ERROR:
      priority = ANDROID_LOG_ERROR;
      break;
    case LOG_FATAL:
      priority = ANDROID_LOG_FATAL;
      break;
  }
  __android_log_write(priority, ANDROID_LOG_TAG, stream_.str().c_str());
#elif defined(OS_IOS)
  syslog(LOG_ALERT, "%s", stream_.str().c_str());
#else
  std::cerr << stream_.str();
  std::cerr.flush();
#endif

  if (severity_ >= LOG_FATAL)
    BreakDebugger();
}

bool LogFirstNState::ShouldLog(uint32_t n) {
  const uint32_t counter_value = counter_.fetch_add(1, std::memory_order_relaxed);
  return counter_value < n;
}

int GetVlogVerbosity() { return std::max(-1, LOG_INFO - GetMinLogLevel()); }

bool ShouldCreateLogMessage(LogSeverity severity) { return severity >= GetMinLogLevel(); }

}  // namespace fxl
