// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_CPP_MACROS_H_
#define LIB_SYSLOG_CPP_MACROS_H_

#if defined(__Fuchsia__)
#include <zircon/types.h>
#endif

#include <lib/fit/variant.h>
#include <lib/syslog/cpp/log_level.h>

#include <limits>
#include <sstream>
#include <vector>

namespace syslog {

class LogValue;
class LogField;

template <typename T>
LogValue ToLogValue(T);

class LogValue {
 public:
  LogValue(std::nullptr_t n) : value_(n) {}
  LogValue(std::initializer_list<LogValue> list) : value_(list) {}
  LogValue(std::initializer_list<LogField> obj) : value_(obj) {}
  LogValue(std::string msg) : value_(msg) {}
  LogValue(int i) : value_(i) {}
  template <typename T>
  LogValue(T t) : LogValue(std::move(ToLogValue(t))) {}

  std::string ToString(bool quote_if_string = false) const;
  void Log(::syslog::LogSeverity severity, const char* file, unsigned int line,
           const char* condition, const char* tag) const;

  operator bool() const { return !fit::holds_alternative<std::nullptr_t>(value_); }

  const std::string* string_value() const {
    if (fit::holds_alternative<std::string>(value_)) {
      return &fit::get<std::string>(value_);
    }

    return nullptr;
  }

  const int64_t* int_value() const {
    if (fit::holds_alternative<int64_t>(value_)) {
      return &fit::get<int64_t>(value_);
    }

    return nullptr;
  }

  const std::vector<LogField>* fields() const {
    if (fit::holds_alternative<std::vector<LogField>>(value_)) {
      return &fit::get<std::vector<LogField>>(value_);
    }

    return nullptr;
  }

 private:
  fit::variant<std::nullptr_t, std::string, int64_t, std::vector<LogValue>, std::vector<LogField>>
      value_;
};

class LogField {
 public:
  LogField(std::string key, LogValue value) : key_(std::move(key)), value_(std::move(value)) {}

  std::string ToString() const;

  const std::string& key() const { return key_; }
  const LogValue& value() const { return value_; }

 private:
  std::string key_;
  LogValue value_;
};

class LogKey {
 public:
  LogKey(std::string key) : key_(std::move(key)) {}

  LogField operator=(LogValue&& message) const { return LogField(key_, std::move(message)); }

 private:
  std::string key_;
};

LogKey operator"" _k(const char* k, unsigned long sz);

template <>
inline LogValue ToLogValue(std::nullptr_t msg) {
  return ToLogValue(msg);
}

inline LogValue ToLogValue(std::string msg) { return LogValue(std::move(msg)); }

template <>
inline LogValue ToLogValue(const char* msg) {
  return ToLogValue(std::string(msg));
}

template <>
inline LogValue ToLogValue(int foo) {
  return ToLogValue(foo);
}

inline LogValue ToLogValue(std::initializer_list<LogValue> list) { return LogValue(list); }

inline LogValue ToLogValue(std::initializer_list<LogField> obj) { return LogValue(obj); }

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
  const char* condition_;
  const char* tag_;
#if defined(__Fuchsia__)
  const zx_status_t status_;
#endif
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

}  // namespace syslog

#define FX_SLOG(severity)                                                                    \
  !FX_LOG_IS_ON(severity) ? (void)0 : ([](const char* tag, ::syslog::LogValue v = nullptr) { \
    v.Log(::syslog::LOG_##severity, __FILE__, __LINE__, nullptr, tag);                       \
  })

#define FX_LOG_STREAM(severity, tag) \
  ::syslog::LogMessage(::syslog::LOG_##severity, __FILE__, __LINE__, nullptr, tag).stream()

#define FX_LOG_STREAM_STATUS(severity, status, tag) \
  ::syslog::LogMessage(::syslog::LOG_##severity, __FILE__, __LINE__, nullptr, tag, status).stream()

#define FX_LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::syslog::LogMessageVoidify() & (stream)

#define FX_EAT_STREAM_PARAMETERS(ignored) \
  true || (ignored)                       \
      ? (void)0                           \
      : ::syslog::LogMessageVoidify() &   \
            ::syslog::LogMessage(::syslog::LOG_FATAL, 0, 0, nullptr, nullptr).stream()

#define FX_LOG_IS_ON(severity) (::syslog::ShouldCreateLogMessage(::syslog::LOG_##severity))

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
#define FX_FIRST_N(n, log_statement)                                                            \
  for (bool do_log = true; do_log; do_log = false)                                              \
    for (static ::syslog::LogFirstNState internal_state; do_log && internal_state.ShouldLog(n); \
         do_log = false)                                                                        \
  log_statement
#define FX_LOGS_FIRST_N(severity, n) FX_FIRST_N(n, FX_LOGS(severity))
#define FX_LOGST_FIRST_N(severity, n, tag) FX_FIRST_N(n, FX_LOGST(severity, tag))

#define FX_CHECK(condition) FX_CHECKT(condition, nullptr)

#define FX_CHECKT(condition, tag)                                                              \
  FX_LAZY_STREAM(                                                                              \
      ::syslog::LogMessage(::syslog::LOG_FATAL, __FILE__, __LINE__, #condition, tag).stream(), \
      !(condition))

// The VLOG macros log with translated verbosities

// Get the severity corresponding to the given verbosity. Note that
// verbosity relative to the default severity and can be thought of
// as incrementally "more vebose than" the baseline.
static inline syslog::LogSeverity GetSeverityFromVerbosity(int verbosity) {
  // Clamp verbosity scale to the interstitial space between INFO and DEBUG
  if (verbosity < 0) {
    verbosity = 0;
  } else {
    int max_verbosity = (syslog::LOG_INFO - syslog::LOG_DEBUG) / syslog::LogVerbosityStepSize;
    if (verbosity > max_verbosity) {
      verbosity = max_verbosity;
    }
  }
  int severity = syslog::LOG_INFO - (verbosity * syslog::LogVerbosityStepSize);
  if (severity < syslog::LOG_DEBUG + 1) {
    return syslog::LOG_DEBUG + 1;
  }
  return static_cast<syslog::LogSeverity>(severity);
}

#define FX_VLOG_IS_ON(verbose_level) (verbose_level <= ::syslog::GetVlogVerbosity())

#define FX_VLOG_STREAM(verbose_level, tag)                                                        \
  ::syslog::LogMessage(GetSeverityFromVerbosity(verbose_level), __FILE__, __LINE__, nullptr, tag) \
      .stream()

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

#endif  // LIB_SYSLOG_CPP_MACROS_H_
