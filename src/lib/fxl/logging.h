// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_LOGGING_H_
#define SRC_LIB_FXL_LOGGING_H_

#if defined(__Fuchsia__)
#include <lib/syslog/global.h>
#include <zircon/types.h>
#endif

#include <limits>
#include <sstream>

#include "src/lib/fxl/log_level.h"
#include "src/lib/fxl/macros.h"

namespace fxl {

class LogMessageVoidify {
 public:
  void operator&(std::ostream&) {}
};

class LogMessage {
 public:
  LogMessage(LogSeverity severity, const char* file, int line, const char* condition,
             const char* tag
#if defined(__Fuchsia__)
             ,
             zx_status_t status = std::numeric_limits<zx_status_t>::max()
#endif
  );
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  const LogSeverity severity_;
  const char* file_;
  const int line_;
  const char* tag_;
#if defined(__Fuchsia__)
  const zx_status_t status_;
#endif

  FXL_DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

// LogFirstNState is used by the macro FX_LOGS_FIRST_N below.
class LogFirstNState {
 public:
  bool ShouldLog(uint32_t n);

 private:
  std::atomic<uint32_t> counter_{0};
};

// Gets the FX_VLOGS default verbosity level.
int GetVlogVerbosity();

// Returns true if |severity| is at or above the current minimum log level.
// LOG_FATAL and above is always true.
bool ShouldCreateLogMessage(LogSeverity severity);

}  // namespace fxl

#define FX_LOG_STREAM(severity, tag) \
  ::fxl::LogMessage(::fxl::LOG_##severity, __FILE__, __LINE__, nullptr, tag).stream()

#define FX_LOG_STREAM_STATUS(severity, status, tag) \
  ::fxl::LogMessage(::fxl::LOG_##severity, __FILE__, __LINE__, nullptr, tag, status).stream()

#define FX_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::fxl::LogMessageVoidify() & (stream)

#define FX_EAT_STREAM_PARAMETERS(ignored)          \
  true || (ignored) ? (void)0                      \
                    : ::fxl::LogMessageVoidify() & \
                          ::fxl::LogMessage(::fxl::LOG_FATAL, 0, 0, nullptr, nullptr).stream()

#define FX_LOG_IS_ON(severity) (::fxl::ShouldCreateLogMessage(::fxl::LOG_##severity))

#define FX_LOGS(severity) FX_LOGST(severity, nullptr)

#define FX_LOGST(severity, tag) FX_LAZY_STREAM(FX_LOG_STREAM(severity, tag), FX_LOG_IS_ON(severity))

#if defined(__Fuchsia__)
#define FX_PLOGST(severity, tag, status) \
  FX_LAZY_STREAM(FX_LOG_STREAM_STATUS(severity, status, tag), FX_LOG_IS_ON(severity))
#define FX_PLOGS(severity, status) FX_PLOGST(severity, nullptr, status)
#endif

// Writes a message to the global logger, the first |n| times that any callsite
// of this macro is invoked. |n| should be a positive integer literal.
// |severity| is one of INFO, WARNING, ERROR, FATAL
//
// Implementation notes:
// The outer for loop is a trick to allow us to introduce a new scope and
// introduce the variable |do_log| into that scope. It executes exactly once.
//
// The inner for loop is a trick to allow us to introduce a new scope and
// introduce the static variable |internal_state| into that new scope. It
// executes either zero or one times.
//
// C++ does not allow us to introduce two new variables into a single for loop
// scope and we need |do_log| so that the inner for loop doesn't execute twice.
#define FX_FIRST_N(n, log_statement)                                                         \
  for (bool do_log = true; do_log; do_log = false)                                           \
    for (static ::fxl::LogFirstNState internal_state; do_log && internal_state.ShouldLog(n); \
         do_log = false)                                                                     \
  log_statement
#define FX_LOGS_FIRST_N(severity, n) FX_FIRST_N(n, FX_LOGS(severity))
#define FX_LOGST_FIRST_N(severity, n, tag) FX_FIRST_N(n, FX_LOGST(severity, tag))

#define FX_CHECK(condition) FX_CHECKT(condition, nullptr)

#define FX_CHECKT(condition, tag)                                                        \
  FX_LAZY_STREAM(                                                                        \
      ::fxl::LogMessage(::fxl::LOG_FATAL, __FILE__, __LINE__, #condition, tag).stream(), \
      !(condition))

#define FX_VLOG_IS_ON(verbose_level) ((verbose_level) <= ::fxl::GetVlogVerbosity())

// The VLOG macros log with negative verbosities.
#define FX_VLOG_STREAM(verbose_level, tag) \
  ::fxl::LogMessage(-verbose_level, __FILE__, __LINE__, nullptr, tag).stream()

#define FX_VLOGS(verbose_level) \
  FX_LAZY_STREAM(FX_VLOG_STREAM(verbose_level, nullptr), FX_VLOG_IS_ON(verbose_level))

#define FX_VLOGST(verbose_level, tag) \
  FX_LAZY_STREAM(FX_VLOG_STREAM(verbose_level, tag), FX_VLOG_IS_ON(verbose_level))

#ifndef NDEBUG
#define FX_DLOGS(severity) FX_LOGS(severity)
#define FX_DVLOGS(verbose_level) FX_VLOGS(verbose_level)
#define FX_DCHECK(condition) FX_CHECK(condition)
#else
#define FX_DLOGS(severity) FX_EAT_STREAM_PARAMETERS(true)
#define FX_DVLOGS(verbose_level) FX_EAT_STREAM_PARAMETERS(true)
#define FX_DCHECK(condition) FX_EAT_STREAM_PARAMETERS(condition)
#endif

#define FX_NOTREACHED() FX_DCHECK(false)

#define FX_NOTIMPLEMENTED() FX_LOGS(ERROR) << "Not implemented in: " << __PRETTY_FUNCTION__

#endif  // SRC_LIB_FXL_LOGGING_H_
