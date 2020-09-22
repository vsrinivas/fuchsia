// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_CPP_LOGGING_BACKEND_H_
#define LIB_SYSLOG_CPP_LOGGING_BACKEND_H_

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

namespace syslog_backend {

void SetLogSettings(const syslog::LogSettings& settings);

void SetLogSettings(const syslog::LogSettings& settings,
                    const std::initializer_list<std::string>& tags);

syslog::LogSeverity GetMinLogLevel();

void WriteLog(syslog::LogSeverity severity, const char* file, unsigned int line, const char* tag,
              const char* condition, const std::string& msg);

void WriteLogValue(syslog::LogSeverity severity, const char* file, unsigned int line,
                   const char* tag, const char* condition, const syslog::LogValue& msg);

}  // namespace syslog_backend

#endif  // LIB_SYSLOG_CPP_LOGGING_BACKEND_H_
