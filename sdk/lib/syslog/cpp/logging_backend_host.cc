// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <unistd.h>

#include <iostream>
#include <sstream>

namespace syslog_backend {

namespace {

// It's OK to keep global state here even though this file is in a source_set because on host
// we don't use shared libraries.
syslog::LogSettings g_log_settings;

const std::string GetNameForLogSeverity(syslog::LogSeverity severity) {
  switch (severity) {
    case syslog::LOG_TRACE:
      return "TRACE";
    case syslog::LOG_DEBUG:
      return "DEBUG";
    case syslog::LOG_INFO:
      return "INFO";
    case syslog::LOG_WARNING:
      return "WARNING";
    case syslog::LOG_ERROR:
      return "ERROR";
    case syslog::LOG_FATAL:
      return "FATAL";
  }

  if (severity > syslog::LOG_DEBUG && severity < syslog::LOG_INFO) {
    std::ostringstream stream;
    stream << "VLOG(" << (syslog::LOG_INFO - severity) << ")";
    return stream.str();
  }

  return "UNKNOWN";
}

}  // namespace

void SetLogSettings(const syslog::LogSettings& settings) {
  g_log_settings.min_log_level = std::min(syslog::LOG_FATAL, settings.min_log_level);

  if (g_log_settings.log_file != settings.log_file) {
    if (!settings.log_file.empty()) {
      int fd = open(settings.log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND);
      if (fd < 0) {
        std::cerr << "Could not open log file: " << settings.log_file << " (" << strerror(errno)
                  << ")" << std::endl;
      } else {
        // Redirect stderr to file.
        if (dup2(fd, STDERR_FILENO) < 0) {
          std::cerr << "Could not set stderr to log file: " << settings.log_file << " ("
                    << strerror(errno) << ")" << std::endl;
        } else {
          g_log_settings.log_file = settings.log_file;
        }
        close(fd);
      }
    }
  }
}

void SetLogSettings(const syslog::LogSettings& settings,
                    const std::initializer_list<std::string>& tags) {
  syslog_backend::SetLogSettings(settings);
}

void SetLogTags(const std::initializer_list<std::string>& tags) {
  // Global tags aren't supported on host.
}

syslog::LogSeverity GetMinLogLevel() { return g_log_settings.min_log_level; }

void WriteLogValue(syslog::LogSeverity severity, const char* file, unsigned int line,
                   const char* tag, const char* condition, const syslog::LogValue& msg) {
  WriteLog(severity, file, line, tag, condition, msg.ToString());
}

void WriteLog(syslog::LogSeverity severity, const char* file, unsigned int line, const char* tag,
              const char* condition, const std::string& msg) {
  if (tag)
    std::cerr << "[" << tag << "] ";

  std::cerr << "[" << GetNameForLogSeverity(severity) << ":" << file << "(" << line << ")]";

  if (condition)
    std::cerr << " Check failed: " << condition << ".";

  std::cerr << std::endl << msg;
  std::cerr.flush();
}

}  // namespace syslog_backend
