// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iostream>

#ifdef __Fuchsia__
#include <zircon/status.h>
#endif

namespace syslog {
namespace {

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

void LogValue::Log(::syslog::LogSeverity severity, const char* file, unsigned int line,
                   const char* condition, const char* tag) const {
  file = severity > LOG_INFO ? StripDots(file) : StripPath(file);
  return syslog_backend::WriteLogValue(severity, file, line, tag, condition, *this);
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
      file_(severity > LOG_INFO ? StripDots(file) : StripPath(file)),
      line_(line),
      condition_(condition),
      tag_(tag)
#if defined(__Fuchsia__)
      ,
      status_(status)
#endif
{
}

LogMessage::~LogMessage() {
#if defined(__Fuchsia__)
  if (status_ != std::numeric_limits<zx_status_t>::max()) {
    stream_ << ": " << status_ << " (" << zx_status_get_string(status_) << ")";
  }
#endif

  syslog_backend::WriteLog(severity_, file_, line_, tag_, condition_, stream_.str());

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
