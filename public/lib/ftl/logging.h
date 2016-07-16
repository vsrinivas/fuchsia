// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_LOGGING_H_
#define LIB_FTL_LOGGING_H_

#include <sstream>

#include "lib/ftl/macros.h"

namespace ftl {

typedef int LogSeverity;

constexpr LogSeverity LOG_INFO = 0;
constexpr LogSeverity LOG_WARNING = 1;
constexpr LogSeverity LOG_ERROR = 2;
constexpr LogSeverity LOG_FATAL = 3;
constexpr LogSeverity LOG_NUM_SEVERITIES = 4;

// LOG_DFATAL is LOG_FATAL in debug mode, ERROR in normal mode
#ifdef NDEBUG
const LogSeverity LOG_DFATAL = LOG_ERROR;
#else
const LogSeverity LOG_DFATAL = LOG_FATAL;
#endif

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

}  // namespace ftl

#define FTL_LOG(severity) \
  ::ftl::LogMessage(::ftl::LOG_##severity, __FILE__, __LINE__, nullptr).stream()

#define FTL_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::ftl::LogMessageVoidify() & (stream)

#define FTL_EAT_STREAM_PARAMETERS \
  true ? (void)0 : ::ftl::LogMessageVoidify() & FTL_LOG_STREAM(FATAL)

#define FTL_CHECK(condition)                                              \
  FTL_LAZY_STREAM(                                                        \
      ::ftl::LogMessage(::ftl::LOG_FATAL, __FILE__, __LINE__, #condition) \
          .stream(),                                                      \
      !(condition))

#ifndef NDEBUG
#define FTL_DCHECK(condition) FTL_CHECK(condition)
#else
#define FTL_DCHECK(condition) FTL_EAT_STREAM_PARAMETERS
#endif

#endif  // LIB_FTL_LOGGING_H_
