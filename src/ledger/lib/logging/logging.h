// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_LOGGING_LOGGING_H_
#define SRC_LEDGER_LIB_LOGGING_LOGGING_H_

#include <iostream>
#include <sstream>

#include "third_party/abseil-cpp/absl/base/log_severity.h"

namespace ledger {

// Transforms a std::ostream result into a boolean with the right priority.
class LogIgnoreStream {
 public:
  bool operator&(const std::ostream&) { return true; }
};

// Holds a stream and a severity, prints it on destruction.
class LogMessage {
 public:
  // Starts a new log message. It will be printed on destruction. |severity| is the severity of the
  // message, |file| must point to a null-terminated string containing the name of the file where
  // the error occured, at line |line|. |condition| is an optional string containing the condition
  // used for CHECK/DCHECK.
  LogMessage(absl::LogSeverity severity, const char* file, int line, const char* condition);
  ~LogMessage();

  // Returns the underlying stream.
  std::ostream& stream();

 private:
  // Stream that accumulates the error message.
  std::ostringstream stream_;
  // Should we abort after printing the error message?
  bool fatal_;
};

// Sets the minimal log severity at which messages are printed. Negative numbers are used for
// verbose logging.
void SetLogSeverity(absl::LogSeverity severity);

// Sets the maximal log verbosity: all non-verbose messages are printed as well as all verbose
// messages whose verbosity is below or equal |verbosity|.
void SetLogVerbosity(int verbosity);

// Returns the current minimum log severity, or |LogSeverity::kInfo| by default.
absl::LogSeverity GetLogSeverity();

}  // namespace ledger

// Macros for log severities.
#define LEDGER_LOGGING_INTERNAL_INFO ::absl::LogSeverity::kInfo
#define LEDGER_LOGGING_INTERNAL_WARNING ::absl::LogSeverity::kWarning
#define LEDGER_LOGGING_INTERNAL_ERROR ::absl::LogSeverity::kError
#define LEDGER_LOGGING_INTERNAL_FATAL ::absl::LogSeverity::kFatal

#define LEDGER_LOG_CONDITIONAL(level, enabled, condition, condition_str) \
  !(enabled) || (level) < ::ledger::GetLogSeverity() || (condition) ||   \
      ::ledger::LogIgnoreStream() &                                      \
          ::ledger::LogMessage((level), __FILE__, __LINE__, (condition_str)).stream()

// Logs a message at severity |LOG_level|. If |level| is FATAL, aborts after printing.
#define LEDGER_LOG(level) \
  LEDGER_LOG_CONDITIONAL(LEDGER_LOGGING_INTERNAL_##level, true, false, nullptr)
// Logs a message at verbosity level |level|.
#define LEDGER_VLOG(level) \
  LEDGER_LOG_CONDITIONAL((::absl::LogSeverity) - (level), true, false, nullptr)
// Logs a mesage and aborts if |condition| is false.
#define LEDGER_CHECK(condition) \
  LEDGER_LOG_CONDITIONAL(::absl::LogSeverity::kFatal, true, condition, #condition)

// Debug macros. LEDGER_DCHECK behaves like LEDGER_CHECK in debug builds, and is ignored in
// release builds.
#ifndef NDEBUG
#define LEDGER_DEBUG true
#else
#define LEDGER_DEBUG false
#endif
#define LEDGER_DCHECK(condition) \
  LEDGER_LOG_CONDITIONAL(::absl::LogSeverity::kFatal, LEDGER_DEBUG, condition, #condition)

// Asserts that this code path is not reachable. This is only checked in debug mode.
#define LEDGER_NOTREACHED() LEDGER_DCHECK(false) << "Unreachable. "

// Prints an error message, but does not crash.
#define LEDGER_NOTIMPLEMENTED() LEDGER_LOG(ERROR) << "Not implemented. "

#endif  // SRC_LEDGER_LIB_LOGGING_LOGGING_H_
