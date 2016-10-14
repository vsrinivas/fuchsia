// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_LOGGING_H_
#define LIB_FTL_LOGGING_H_

#include <sstream>

#include "lib/ftl/log_level.h"
#include "lib/ftl/macros.h"

namespace ftl {

class LogMessageVoidify {
 public:
  void operator&(std::ostream&) {}
};

class LogMessage {
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

  FTL_DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

// Gets the FTL_VLOG default verbosity level.
int GetVlogVerbosity();

}  // namespace ftl

#define FTL_LOG(severity) \
  ::ftl::LogMessage(::ftl::LOG_##severity, __FILE__, __LINE__, nullptr).stream()

#define FTL_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::ftl::LogMessageVoidify() & (stream)

#define FTL_EAT_STREAM_PARAMETERS(ignored) \
  true || (ignored)                        \
      ? (void)0                            \
      : ::ftl::LogMessageVoidify() &       \
            ::ftl::LogMessage(::ftl::LOG_FATAL, 0, 0, nullptr).stream()

#define FTL_CHECK(condition)                                              \
  FTL_LAZY_STREAM(                                                        \
      ::ftl::LogMessage(::ftl::LOG_FATAL, __FILE__, __LINE__, #condition) \
          .stream(),                                                      \
      !(condition))

#define FTL_VLOG_IS_ON(verbose_level) \
  ((verbose_level) <= ::ftl::GetVlogVerbosity())

// The VLOG macros log with negative verbosities.
#define FTL_VLOG_STREAM(verbose_level) \
  ::ftl::LogMessage(-verbose_level, __FILE__, __LINE__, nullptr).stream()

#define FTL_VLOG(verbose_level) \
  FTL_LAZY_STREAM(FTL_VLOG_STREAM(verbose_level), FTL_VLOG_IS_ON(verbose_level))

#ifndef NDEBUG
#define FTL_DLOG(severity) FTL_LOG(severity)
#define FTL_DCHECK(condition) FTL_CHECK(condition)
#else
#define FTL_DLOG(severity) FTL_EAT_STREAM_PARAMETERS(true)
#define FTL_DCHECK(condition) FTL_EAT_STREAM_PARAMETERS(condition)
#endif

#define FTL_NOTREACHED() FTL_DCHECK(false)

#define FTL_NOTIMPLEMENTED() FTL_LOG(ERROR) << "Not implemented in: " << __PRETTY_FUNCTION__

#endif  // LIB_FTL_LOGGING_H_
