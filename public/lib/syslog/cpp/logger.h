// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_CPP_LOGGER_H_
#define LIB_SYSLOG_CPP_LOGGER_H_

#include <initializer_list>
#include <ostream>
#include <sstream>
#include <string>

#include <syslog/global.h>

namespace syslog {
namespace internal {

class LogMessageVoidify {
 public:
  void operator&(std::ostream&) {}
};

class LogMessage {
 public:
  LogMessage(fx_log_severity_t severity, const char* file, int line,
             const char* tag, const char* condition);
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  const fx_log_severity_t severity_;
  const char* file_;
  const int line_;
  const char* tag_;

  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
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

#define _FX_LOG_STREAM(severity, tag)                                   \
  ::syslog::internal::LogMessage((severity), __FILE__, __LINE__, (tag), \
                                 nullptr)                               \
      .stream()

#define FX_LOG_STREAM(severity, tag) _FX_LOG_STREAM(FX_LOG_##severity, (tag))

#define FX_LOG_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::syslog::internal::LogMessageVoidify() & (stream)

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_LOGST(severity, tag)                      \
  FX_LOG_LAZY_STREAM(FX_LOG_STREAM(severity, (tag)), \
                     FX_LOG_IS_ENABLED(severity))

// Writes a message to the global logger.
// |severity| is one of FX_LOG_DEBUG, FX_LOG_INFO, FX_LOG_WARNING,
// FX_LOG_ERROR, FX_LOG_FATAL
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_LOGST_WITH_SEVERITY(severity, tag)           \
  FX_LOG_LAZY_STREAM(_FX_LOG_STREAM((severity), (tag)), \
                     fx_log_is_enabled((severity)))

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
#define FX_LOGS(severity) FX_LOGST(severity, nullptr)

// Writes a message to the global logger.
// |severity| is one of FX_LOG_DEBUG, FX_LOG_INFO, FX_LOG_WARNING,
// FX_LOG_ERROR, FX_LOG_FATAL
#define FX_LOGS_WITH_SEVERITY(severity) \
  FX_LOGST_WITH_SEVERITY((severity), nullptr)

// Writes error message to the global logger if |condition| fails.
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_CHECKT(condition, tag)                                              \
  FX_LOG_LAZY_STREAM(::syslog::internal::LogMessage(FX_LOG_FATAL, __FILE__,    \
                                                    __LINE__, tag, #condition) \
                         .stream(),                                            \
                     !(condition))

// Writes error message to the global logger if |condition| fails.
#define FX_CHECK(condition) FX_CHECKT(condition, nullptr)

// VLOG macros log with negative verbosities.
#define FX_VLOG_STREAM(verbose_level, tag)                                    \
  ::syslog::internal::LogMessage(-(verbose_level), __FILE__, __LINE__, (tag), \
                                 nullptr)                                     \
      .stream()

#define FX_VLOGST(verbose_level, tag)                        \
  FX_LOG_LAZY_STREAM(FX_VLOG_STREAM((verbose_level), (tag)), \
                     FX_VLOG_IS_ENABLED((verbose_level)))

#define FX_VLOGS(verbose_level) FX_VLOGST((verbose_level), nullptr)

#endif  // LIB_SYSLOG_CPP_LOGGER_H_
