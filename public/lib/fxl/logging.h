// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_LOGGING_H_
#define LIB_FXL_LOGGING_H_

#include <sstream>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/log_level.h"
#include "lib/fxl/macros.h"

namespace fxl {

class LogMessageVoidify {
 public:
  void operator&(std::ostream&) {}
};

class FXL_EXPORT LogMessage {
 public:
  LogMessage(LogSeverity severity,
             const char* file,
             int line,
             const char* condition);
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  const LogSeverity severity_;
  const char* file_;
  const int line_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

// Gets the FXL_VLOG default verbosity level.
FXL_EXPORT int GetVlogVerbosity();

// Returns true if |severity| is at or above the current minimum log level.
// LOG_FATAL and above is always true.
FXL_EXPORT bool ShouldCreateLogMessage(LogSeverity severity);

}  // namespace fxl

#define FXL_LOG_STREAM(severity) \
  ::fxl::LogMessage(::fxl::LOG_##severity, __FILE__, __LINE__, nullptr).stream()

#define FXL_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::fxl::LogMessageVoidify() & (stream)

#define FXL_EAT_STREAM_PARAMETERS(ignored) \
  true || (ignored)                        \
      ? (void)0                            \
      : ::fxl::LogMessageVoidify() &       \
            ::fxl::LogMessage(::fxl::LOG_FATAL, 0, 0, nullptr).stream()

#define FXL_LOG_IS_ON(severity) \
  (::fxl::ShouldCreateLogMessage(::fxl::LOG_##severity))

#define FXL_LOG(severity) \
  FXL_LAZY_STREAM(FXL_LOG_STREAM(severity), FXL_LOG_IS_ON(severity))

#define FXL_CHECK(condition)                                              \
  FXL_LAZY_STREAM(                                                        \
      ::fxl::LogMessage(::fxl::LOG_FATAL, __FILE__, __LINE__, #condition) \
          .stream(),                                                      \
      !(condition))

#define FXL_VLOG_IS_ON(verbose_level) \
  ((verbose_level) <= ::fxl::GetVlogVerbosity())

// The VLOG macros log with negative verbosities.
#define FXL_VLOG_STREAM(verbose_level) \
  ::fxl::LogMessage(-verbose_level, __FILE__, __LINE__, nullptr).stream()

#define FXL_VLOG(verbose_level) \
  FXL_LAZY_STREAM(FXL_VLOG_STREAM(verbose_level), FXL_VLOG_IS_ON(verbose_level))

#ifndef NDEBUG
#define FXL_DLOG(severity) FXL_LOG(severity)
#define FXL_DCHECK(condition) FXL_CHECK(condition)
#else
#define FXL_DLOG(severity) FXL_EAT_STREAM_PARAMETERS(true)
#define FXL_DCHECK(condition) FXL_EAT_STREAM_PARAMETERS(condition)
#endif

#define FXL_NOTREACHED() FXL_DCHECK(false)

#define FXL_NOTIMPLEMENTED() \
  FXL_LOG(ERROR) << "Not implemented in: " << __PRETTY_FUNCTION__

#endif  // LIB_FXL_LOGGING_H_
