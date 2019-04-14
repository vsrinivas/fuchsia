// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger.h"

#include <lib/syslog/global.h>

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

// Note that this implementation allows a data race on counter_, but
// we consider that harmless because, as specified by the comments on
// FX_LOGS_FIRST_N, we allow for the possibility that the message might get
// logged more than |n| times if a single callsite of that macro is invoked by
// multiple threads.
bool LogFirstNState::ShouldLog(int n) {
  const int32_t counter_value = counter_.load(std::memory_order_relaxed);
  if (counter_value < n) {
    counter_.store(counter_value + 1, std::memory_order_relaxed);
    return true;
  }
  return false;
}

}  // namespace internal
}  // namespace syslog
