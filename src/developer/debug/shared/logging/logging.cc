// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/logging.h"

#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>

#include <iostream>

namespace debug {

namespace {

LogBackend* log_backend = nullptr;
bool log_enable_syslog = true;

syslog::LogSeverity ConvertSeverity(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::kInfo:
      return syslog::LOG_INFO;
    case LogSeverity::kWarn:
      return syslog::LOG_WARNING;
    case LogSeverity::kError:
      return syslog::LOG_ERROR;
  }
}

}  // namespace

LogStatement::~LogStatement() {
  if (log_backend) {
    log_backend->WriteLog(severity_, location_, stream_.str());
  }
  if (log_enable_syslog) {
    auto severity = ConvertSeverity(severity_);
    if (syslog::ShouldCreateLogMessage(severity)) {
      syslog::LogMessage(severity, location_.file(), static_cast<int>(location_.line()), nullptr,
                         nullptr)
              .stream()
          << stream_.str();
    }
  }
}

void LogBackend::Set(LogBackend* backend, bool enable_syslog) {
  log_backend = backend;
  log_enable_syslog = enable_syslog;
}

void LogBackend::Unset() { Set(nullptr, true); }

}  // namespace debug
