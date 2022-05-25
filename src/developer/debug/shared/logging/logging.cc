// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/logging.h"

#include <stdio.h>

#include <iostream>

namespace debug {

namespace {

LogSink* log_sink = nullptr;

const char* SeverityToName(LogSeverity severity) {
  static_assert(static_cast<int>(LogSeverity::kInfo) == 0);
  static_assert(static_cast<int>(LogSeverity::kWarn) == 1);
  static_assert(static_cast<int>(LogSeverity::kError) == 2);

  static const char* kSeverityNames[] = {"INFO", "WARN", "ERROR"};
  return kSeverityNames[static_cast<int>(severity)];
}

}  // namespace

LogStatement::~LogStatement() {
  if (log_sink) {
    log_sink->WriteLog(severity_, stream_.str());
  } else {
    std::cerr << SeverityToName(severity_) << ": " << stream_.str() << std::endl;
  }
}

void LogSink::Set(LogSink* sink) { log_sink = sink; }

void LogSink::Unset() { log_sink = nullptr; }

}  // namespace debug
