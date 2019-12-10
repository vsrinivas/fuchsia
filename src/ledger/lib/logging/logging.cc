// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/logging/logging.h"

#include <algorithm>
#include <iostream>
#include <string>

#include "third_party/abseil-cpp/absl/base/log_severity.h"

namespace ledger {

namespace {
// The (global) minimum log severity.
absl::LogSeverity min_log_severity = absl::LogSeverity::kInfo;

std::string PrintSeverity(absl::LogSeverity severity) {
  if ((int)severity >= 0) {
    return absl::LogSeverityName(severity);
  } else {
    return "VERBOSE" + std::to_string(-(int)severity);
  }
}

// Removes all slashes and dots at the beginning of the given path.
const char* StripLeadingDots(const char* file) {
  while (file[0] == '.' || file[0] == '/') {
    file++;
  }
  return file;
}

}  // namespace

void SetLogSeverity(absl::LogSeverity severity) {
  min_log_severity = std::min(severity, absl::LogSeverity::kFatal);
}
void SetLogVerbosity(int verbosity) {
  min_log_severity = std::min((absl::LogSeverity)-verbosity, absl::LogSeverity::kFatal);
}
absl::LogSeverity GetLogSeverity() { return min_log_severity; }

LogMessage::LogMessage(absl::LogSeverity severity, const char* file, int line,
                       const char* condition) {
  fatal_ = (severity >= absl::LogSeverity::kFatal);
  stream_ << "[" << PrintSeverity(severity) << ":" << StripLeadingDots(file) << "(" << line
          << ")] ";
  if (condition) {
    stream_ << "Check failed: " << condition << ". ";
  }
}

std::ostream& LogMessage::stream() { return stream_; }

LogMessage::~LogMessage() {
  std::cerr << stream_.str() << std::endl;
  std::cerr.flush();
  if (fatal_) {
    abort();
  }
}

}  // namespace ledger
