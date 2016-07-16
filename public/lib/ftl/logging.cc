// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/ftl/debug/debugger.h"
#include "lib/ftl/logging.h"

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
  std::cerr << stream_.str() << std::endl;
  std::cerr.flush();

  if (severity_ >= LOG_FATAL)
    BreakDebugger();
}

}  // namespace ftl
