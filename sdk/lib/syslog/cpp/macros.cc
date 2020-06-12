// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iostream>

#if defined(__Fuchsia__)
#include <lib/syslog/global.h>
#include <zircon/status.h>
#endif

namespace syslog {
namespace {

#ifndef __Fuchsia__

const std::string GetNameForLogSeverity(LogSeverity severity) {
  switch (severity) {
    case LOG_TRACE:
      return "TRACE";
    case LOG_DEBUG:
      return "DEBUG";
    case LOG_INFO:
      return "INFO";
    case LOG_WARNING:
      return "WARNING";
    case LOG_ERROR:
      return "ERROR";
    case LOG_FATAL:
      return "FATAL";
  }

  if (severity > LOG_DEBUG && severity < LOG_INFO) {
    std::ostringstream stream;
    stream << "VLOG(" << (LOG_INFO - severity) << ")";
    return stream.str();
  }

  return "UNKNOWN";
}
#endif

const char* StripDots(const char* path) {
  while (strncmp(path, "../", 3) == 0)
    path += 3;
  return path;
}

const char* StripPath(const char* path) {
  auto p = strrchr(path, '/');
  if (p)
    return p + 1;
  else
    return path;
}

}  // namespace

std::string LogValue::ToString(bool quote_if_string) const {
  if (fit::holds_alternative<std::nullptr_t>(value_)) {
    return "";
  } else if (fit::holds_alternative<std::string>(value_)) {
    auto& str = fit::get<std::string>(value_);
    if (!quote_if_string) {
      return str;
    } else {
      return "\"" + str + "\"";
    }
  } else if (fit::holds_alternative<int64_t>(value_)) {
    return std::to_string(fit::get<int64_t>(value_));
  } else if (fit::holds_alternative<std::vector<LogValue>>(value_)) {
    auto& list = fit::get<std::vector<LogValue>>(value_);
    std::string ret = "[";

    for (const auto& item : list) {
      if (ret.size() > 1) {
        ret += ", ";
      }

      ret += item.ToString(true);
    }

    return ret + "]";
  } else {
    auto& obj = fit::get<std::vector<LogField>>(value_);
    std::string ret = "{";

    for (const auto& field : obj) {
      if (ret.size() > 1) {
        ret += ", ";
      }

      ret += field.ToString();
    }

    return ret + "}";
  }
}

std::string LogField::ToString() const { return "\"" + key_ + "\": " + value_.ToString(true); }

LogKey operator"" _k(const char* k, unsigned long sz) { return LogKey(std::string(k, sz)); }

LogMessage::LogMessage(LogSeverity severity, const char* file, int line, const char* condition,
                       const char* tag
#if defined(__Fuchsia__)
                       ,
                       zx_status_t status
#endif
                       )
    : severity_(severity),
      file_(file),
      line_(line),
      tag_(tag)
#if defined(__Fuchsia__)
      ,
      status_(status)
#endif
{
#if !defined(__Fuchsia__)
  if (tag)
    stream_ << "[" << tag_ << "] ";
#endif
  stream_ << "[";
  // With syslog the severity is included in the metadata so no need to add it
  // to the log message itself.
#ifndef __Fuchsia__
  stream_ << GetNameForLogSeverity(severity) << ":";
#endif
  stream_ << (severity > LOG_INFO ? StripDots(file_) : StripPath(file_)) << "(" << line_ << ")] ";

  if (condition)
    stream_ << "Check failed: " << condition << ". ";
}

LogMessage::~LogMessage() {
#if defined(__Fuchsia__)
  if (status_ != std::numeric_limits<zx_status_t>::max()) {
    stream_ << ": " << status_ << " (" << zx_status_get_string(status_) << ")";
  }
#else
  stream_ << std::endl;
#endif

#if defined(__Fuchsia__)
  // Write fatal logs to stderr as well because death tests sometimes verify a certain
  // log message was printed prior to the crash.
  // TODO(samans): Convert tests to not depend on stderr. https://fxbug.dev/49593
  if (severity_ == LOG_FATAL)
    std::cerr << stream_.str() << std::endl;
  fx_logger_t* logger = fx_log_get_logger();
  fx_logger_log(logger, severity_, tag_, stream_.str().c_str());
#else
  std::cerr << stream_.str();
  std::cerr.flush();
#endif

  if (severity_ >= LOG_FATAL)
    __builtin_debugtrap();
}

bool LogFirstNState::ShouldLog(uint32_t n) {
  const uint32_t counter_value = counter_.fetch_add(1, std::memory_order_relaxed);
  return counter_value < n;
}

int GetVlogVerbosity() {
  int min_level = GetMinLogLevel();
  if (min_level < LOG_INFO && min_level > LOG_DEBUG) {
    return LOG_INFO - min_level;
  }
  return 0;
}

bool ShouldCreateLogMessage(LogSeverity severity) { return severity >= GetMinLogLevel(); }

}  // namespace syslog
