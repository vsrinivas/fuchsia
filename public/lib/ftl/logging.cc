// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>

#include "lib/ftl/build_config.h"
#include "lib/ftl/debug/debugger.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"

#if defined(OS_ANDROID)
#include <android/log.h>
#endif

namespace ftl {
namespace {

const char* const kLogSeverityNames[LOG_NUM_SEVERITIES] = {"INFO", "WARNING",
                                                           "ERROR", "FATAL"};

const char* GetNameForLogSeverity(LogSeverity severity) {
  if (severity >= LOG_INFO && severity < LOG_NUM_SEVERITIES)
    return kLogSeverityNames[severity];
  return "UNKNOWN";
}

}  // namespace

LogMessage::LogMessage(LogSeverity severity,
                       const char* file,
                       int line,
                       const char* condition)
    : severity_(severity), file_(file), line_(line) {
  stream_ << "[";
  if (severity >= LOG_INFO)
    stream_ << GetNameForLogSeverity(severity);
  else
    stream_ << "VERBOSE" << -severity;
  stream_ << ":" << file_ << "(" << line_ << ")] ";
  if (condition)
    stream_ << "Check failed: " << condition << ". ";
}

LogMessage::~LogMessage() {
  stream_ << std::endl;

#if defined(OS_ANDROID)
  android_LogPriority priority =
      (severity_ < 0) ? ANDROID_LOG_VERBOSE : ANDROID_LOG_UNKNOWN;
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
#else
  std::cerr << stream_.str();
  std::cerr.flush();
#endif

  if (severity_ >= LOG_FATAL)
    BreakDebugger();
}

int GetVlogVerbosity() {
  return std::max(-1, LOG_INFO - GetMinLogLevel());
}

bool ShouldCreateLogMessage(LogSeverity severity) {
  return severity >= GetMinLogLevel();
}

}  // namespace ftl
