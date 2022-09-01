// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_LOGGING_LOGGING_H_
#define SRC_DEVELOPER_DEBUG_SHARED_LOGGING_LOGGING_H_

// This header is meant to be the hub of debug logging: timers, logging, etc. There is no need to
// include the other headers directly.

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/logging/macros.h"

namespace debug {

// Use this macro instead of FX_LOGS so that
//   1) In zxdb, the console won't be messed up.
//   2) In debug_agent, the log could be redirected to zxdb.
//
// Only three severities are supported: LOGS(Info), LOGS(Warn) and LOGS(Error).
// For Debug, please use DEBUG_LOG. For Fatal, consider using FX_CHECK or FX_LOGS(FATAL).
#define LOGS(severity) ::debug::LogStatement(::debug::LogSeverity::k##severity, FROM_HERE).stream()

// Use DEBUG_LOG to print logs for debugging.
// Example:
//
// DEBUG_LOG(Job) << "Some job statement.";
// DEBUG_LOG(MessageLoop) << "Some event with id " << id;
//
// If the logging will occur on some other function, you can pass the location into the other macro:
//
// if (err.has_error())
//   LogSomewhereElse(FROM_HERE, LogCategory::kAgent, error.msg());
//
//  ...
//
// void LogSomewhereElse(FileLineFunction location, LogCategory category, std::string msg) {
//    ...
//    DEBUG_LOG_WITH_LOCATION(category, location) << msg;
// }
//
#define DEBUG_LOG_WITH_LOCATION(category, location)                                               \
  ::debug::DebugLogStatement STRINGIFY(__debug_log, __LINE__)(location,                           \
                                                              ::debug::LogCategory::k##category); \
  STRINGIFY(__debug_log, __LINE__).stream()
#define DEBUG_LOG(category) DEBUG_LOG_WITH_LOCATION(category, FROM_HERE)

// Implementation ----------------------------------------------------------------------------------
enum class LogSeverity {
  kInfo,
  kWarn,
  kError,
};

class LogStatement {
 public:
  LogStatement(LogSeverity severity, const FileLineFunction& location)
      : severity_(severity), location_(location) {}
  ~LogStatement();
  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  LogSeverity severity_;
  FileLineFunction location_;
};

// Should be implemented by e.g. |zxdb::Console|.
class LogBackend {
 public:
  virtual void WriteLog(LogSeverity severity, const FileLineFunction& location,
                        std::string log) = 0;

  // Set the backend to |backend|. If |enable_syslog| is true, logs will also be sent to the syslog.
  static void Set(LogBackend* backend, bool enable_syslog);
  static void Unset();
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_LOGGING_LOGGING_H_
