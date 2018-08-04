// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger.h"

#include <syslog/global.h>

namespace syslog {
namespace {

const char* StripDots(const char* path) {
  while (strncmp(path, "../", 3) == 0)
    path += 3;
  return path;
}

const char* StripPath(const char* path) {
  const char* p = strrchr(path, '/');
  if (p)
    return p + 1;
  else
    return path;
}

}  // namespace

namespace internal {

LogMessage::LogMessage(fx_log_severity_t severity, const char* file, int line,
                       const char* tag, const char* condition)
    : severity_(severity), file_(file), line_(line), tag_(tag) {
  stream_ << (severity > FX_LOG_INFO ? StripDots(file_) : StripPath(file_))
          << "(" << line_ << "): ";

  if (condition)
    stream_ << "Check failed: " << condition << ": ";
}

LogMessage::~LogMessage() {
  fx_logger_t* logger = fx_log_get_logger();
  if (logger) {
    fx_logger_log(logger, severity_, tag_, stream_.str().c_str());
  }
}

}  // namespace internal
}  // namespace syslog
