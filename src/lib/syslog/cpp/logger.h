// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_SYSLOG_CPP_LOGGER_H_
#define SRC_LIB_SYSLOG_CPP_LOGGER_H_

#include <lib/syslog/global.h>
#include <stdint.h>
#include <zircon/types.h>

#include <initializer_list>
#include <ostream>
#include <sstream>
#include <string>

namespace syslog {
namespace internal {

class LogMessageVoidify {
 public:
  void operator&(std::ostream&) {}
};

class LogMessage {
 public:
  LogMessage(fx_log_severity_t severity, const char* file, int line, const char* tag,
             zx_status_t status, const char* condition);
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  const fx_log_severity_t severity_;
  const char* file_;
  const int line_;
  const char* tag_;
  const zx_status_t status_;

  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
};

// LogFirstNState is used by the macro FX_LOGS_FIRST_N below.
class LogFirstNState {
 public:
  bool ShouldLog(int n);

 private:
  std::atomic<int32_t> counter_{0};
};

}  // namespace internal

struct LogSettings {
  fx_log_severity_t severity;
  int fd;
};

// Creates default logger from provided tags and settings.
// Returns zx_status_t if it is not able to create logger.
zx_status_t InitLogger(const syslog::LogSettings& settings,
                       const std::initializer_list<std::string>& tags);

// Creates default logger from provided tags.
// Returns zx_status_t if it is not able to create logger.
zx_status_t InitLogger(const std::initializer_list<std::string>& tags);

// Creates default logger.
// Returns zx_status_t if it is not able to create logger.
zx_status_t InitLogger();

}  // namespace syslog

#define _FX_LOG_STREAM(severity, tag, status) \
  ::syslog::internal::LogMessage((severity), __FILE__, __LINE__, (tag), (status), nullptr).stream()

#define FX_LOG_STREAM(severity, tag, status) _FX_LOG_STREAM(FX_LOG_##severity, (tag), (status))

#define FX_LOG_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::syslog::internal::LogMessageVoidify() & (stream)

#define _FX_EAT_STREAM_PARAMETERS(ignored, tag)                                                  \
  true || (ignored) || (tag) != nullptr                                                          \
      ? (void)0                                                                                  \
      : FX_LOG_LAZY_STREAM(::syslog::internal::LogMessage(FX_LOG_FATAL, __FILE__, __LINE__, tag, \
                                                          INT32_MAX, #ignored)                   \
                               .stream(),                                                        \
                           !(ignored))

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_LOGST(severity, tag) \
  FX_LOG_LAZY_STREAM(FX_LOG_STREAM(severity, (tag), INT32_MAX), FX_LOG_IS_ENABLED(severity))

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |status| is a zx_status_t which will be appended in decimal and string forms
// after the message.
#define FX_PLOGST(severity, tag, status) \
  FX_LOG_LAZY_STREAM(FX_LOG_STREAM(severity, (tag), (status)), FX_LOG_IS_ENABLED(severity))

// Writes a message to the global logger.
// |severity| is one of FX_LOG_DEBUG, FX_LOG_INFO, FX_LOG_WARNING,
// FX_LOG_ERROR, FX_LOG_FATAL
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_LOGST_WITH_SEVERITY(severity, tag) \
  FX_LOG_LAZY_STREAM(_FX_LOG_STREAM((severity), (tag), INT32_MAX), fx_log_is_enabled((severity)))

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
#define FX_LOGS(severity) FX_LOGST(severity, nullptr)

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |status| is a zx_status_t which will be appended in decimal and string forms
// after the message.
#define FX_PLOGS(severity, status) FX_PLOGST(severity, nullptr, status)

// Writes a message to the global logger, the first |n| times that any callsite
// of this macro is invoked. |n| should be a positive integer literal. If a
// single callsite is invoked by multiple threads it is possible that the
// number of times the message is written could be greater than |n| but will
// never be greater than n-1 + #(calling threads).
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
//
// Implementation notes:
// The outer for loop is a trick to allow us to introduce a new scope and
// introduce the variable |syslog_internal_do_log| into that scope. It
// executes exactly once.
//
// The inner for loop is a trick to allow us to introduce a new scope
// and introduce the static variable |syslog_internal_state| into that
// new scope. It executes either zero or one times.
//
// C++ does not allow us to introduce two new variables into
// a single for loop scope and we need |syslog_internal_do_log| so that
// the inner for loop doesn't execute twice.
#define FX_LOGS_FIRST_N(severity, n) FX_LOGST_FIRST_N(severity, n, nullptr)

#define FX_LOGST_FIRST_N(severity, n, tag)                                                         \
  for (bool syslog_internal_do_log = true; syslog_internal_do_log; syslog_internal_do_log = false) \
    for (static syslog::internal::LogFirstNState syslog_internal_state;                            \
         syslog_internal_do_log && syslog_internal_state.ShouldLog(n);                             \
         syslog_internal_do_log = false)                                                           \
  FX_LOGST(severity, (tag))

// Writes a message to the global logger.
// |severity| is one of FX_LOG_DEBUG, FX_LOG_INFO, FX_LOG_WARNING,
// FX_LOG_ERROR, FX_LOG_FATAL
#define FX_LOGS_WITH_SEVERITY(severity) FX_LOGST_WITH_SEVERITY((severity), nullptr)

// Writes error message to the global logger if |condition| fails.
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_CHECKT(condition, tag)                                                                  \
  FX_LOG_LAZY_STREAM(                                                                              \
      ::syslog::internal::LogMessage(FX_LOG_FATAL, __FILE__, __LINE__, tag, INT32_MAX, #condition) \
          .stream(),                                                                               \
      !(condition))

// Writes error message to the global logger if |condition| fails.
#define FX_CHECK(condition) FX_CHECKT(condition, nullptr)

// Writes error message to the global logger if |condition| fails in debug
// build.
#ifndef NDEBUG
#define FX_DCHECK(condition) FX_CHECK(condition)
#define FX_DCHECKT(condition, tag) FX_CHECKT(condition, tag)
#else
#define FX_DCHECK(condition) _FX_EAT_STREAM_PARAMETERS(condition, nullptr)
#define FX_DCHECKT(condition, tag) _FX_EAT_STREAM_PARAMETERS(condition, tag)
#endif

// Writes a message to the global logger only in debug builds.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
#ifndef NDEBUG
#define FX_DLOGS(severity) FX_LOGS(severity)
#else
#define FX_DLOGS(severity) _FX_EAT_STREAM_PARAMETERS(true, nullptr)
#endif

#define FX_NOTREACHED() FX_DCHECK(false)

#define FX_NOTIMPLEMENTED() FX_LOGS(ERROR) << "Not implemented in: " << __PRETTY_FUNCTION__

// VLOG macros log with negative verbosities.
#define FX_VLOG_STREAM(verbose_level, tag, status)                                               \
  ::syslog::internal::LogMessage(-(verbose_level), __FILE__, __LINE__, (tag), (status), nullptr) \
      .stream()

#define FX_VLOGST(verbose_level, tag)                                   \
  FX_LOG_LAZY_STREAM(FX_VLOG_STREAM((verbose_level), (tag), INT32_MAX), \
                     FX_VLOG_IS_ENABLED((verbose_level)))

#define FX_VPLOGST(verbose_level, tag, status)                         \
  FX_LOG_LAZY_STREAM(FX_VLOG_STREAM((verbose_level), (tag), (status)), \
                     FX_VLOG_IS_ENABLED((verbose_level)))

#define FX_VLOGS(verbose_level) FX_VLOGST((verbose_level), nullptr)

#define FX_VPLOGS(verbose_level, status) FX_VPLOGST((verbose_level), nullptr, status)

#endif  // SRC_LIB_SYSLOG_CPP_LOGGER_H_
