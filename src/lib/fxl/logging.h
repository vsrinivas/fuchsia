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

// LogFirstNState is used by the macro FXL_LOG_FIRST_N below.
class LogFirstNState {
 public:
  bool ShouldLog(uint32_t n);

 private:
  std::atomic<uint32_t> counter_{0};
};

// Gets the FXL_VLOG default verbosity level.
int GetVlogVerbosity();

// Returns true if |severity| is at or above the current minimum log level.
// LOG_FATAL and above is always true.
bool ShouldCreateLogMessage(LogSeverity severity);

}  // namespace fxl

#define FXL_LOG_STREAM(severity, tag) \
  ::fxl::LogMessage(::fxl::LOG_##severity, __FILE__, __LINE__, nullptr, tag).stream()

#define FXL_LOG_STREAM_STATUS(severity, status, tag) \
  ::fxl::LogMessage(::fxl::LOG_##severity, __FILE__, __LINE__, nullptr, tag, status).stream()

#define FXL_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::fxl::LogMessageVoidify() & (stream)

#define FXL_EAT_STREAM_PARAMETERS(ignored)         \
  true || (ignored) ? (void)0                      \
                    : ::fxl::LogMessageVoidify() & \
                          ::fxl::LogMessage(::fxl::LOG_FATAL, 0, 0, nullptr, nullptr).stream()

#define FXL_LOG_IS_ON(severity) (::fxl::ShouldCreateLogMessage(::fxl::LOG_##severity))

#define FXL_LOG(severity) FXL_LOGT(severity, nullptr)

#define FXL_LOGT(severity, tag) \
  FXL_LAZY_STREAM(FXL_LOG_STREAM(severity, tag), FXL_LOG_IS_ON(severity))

#if defined(__Fuchsia__)
#define FXL_PLOGT(severity, tag, status) \
  FXL_LAZY_STREAM(FXL_LOG_STREAM_STATUS(severity, status, tag), FXL_LOG_IS_ON(severity))
#define FXL_PLOG(severity, status) FXL_PLOGT(severity, nullptr, status)
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
#define FXL_FIRST_N(n, log_statement)                                                        \
  for (bool do_log = true; do_log; do_log = false)                                           \
    for (static ::fxl::LogFirstNState internal_state; do_log && internal_state.ShouldLog(n); \
         do_log = false)                                                                     \
  log_statement
#define FXL_LOG_FIRST_N(severity, n) FXL_FIRST_N(n, FXL_LOG(severity))
#define FXL_LOGT_FIRST_N(severity, n, tag) FXL_FIRST_N(n, FXL_LOGT(severity, tag))

#define FXL_CHECK(condition) FXL_CHECKT(condition, nullptr)

#define FXL_CHECKT(condition, tag)                                                       \
  FXL_LAZY_STREAM(                                                                       \
      ::fxl::LogMessage(::fxl::LOG_FATAL, __FILE__, __LINE__, #condition, tag).stream(), \
      !(condition))

#define FXL_VLOG_IS_ON(verbose_level) ((verbose_level) <= ::fxl::GetVlogVerbosity())

// The VLOG macros log with negative verbosities.
#define FXL_VLOG_STREAM(verbose_level, tag) \
  ::fxl::LogMessage(-verbose_level, __FILE__, __LINE__, nullptr, tag).stream()

#define FXL_VLOG(verbose_level) \
  FXL_LAZY_STREAM(FXL_VLOG_STREAM(verbose_level, nullptr), FXL_VLOG_IS_ON(verbose_level))

#define FXL_VLOGT(verbose_level, tag) \
  FXL_LAZY_STREAM(FXL_VLOG_STREAM(verbose_level, tag), FXL_VLOG_IS_ON(verbose_level))

#ifndef NDEBUG
#define FXL_DLOG(severity) FXL_LOG(severity)
#define FXL_DVLOG(verbose_level) FXL_VLOG(verbose_level)
#define FXL_DCHECK(condition) FXL_CHECK(condition)
#else
#define FXL_DLOG(severity) FXL_EAT_STREAM_PARAMETERS(true)
#define FXL_DVLOG(verbose_level) FXL_EAT_STREAM_PARAMETERS(true)
#define FXL_DCHECK(condition) FXL_EAT_STREAM_PARAMETERS(condition)
#endif

#define FXL_NOTREACHED() FXL_DCHECK(false)

#define FXL_NOTIMPLEMENTED() FXL_LOG(ERROR) << "Not implemented in: " << __PRETTY_FUNCTION__

#endif  // SRC_LIB_FXL_LOGGING_H_
