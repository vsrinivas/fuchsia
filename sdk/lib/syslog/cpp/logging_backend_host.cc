// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <unistd.h>

#include <iostream>

namespace syslog_backend {

namespace {

// It's OK to keep global state here even though this file is in a source_set because on host
// we don't use shared libraries.
syslog::LogSettings g_log_settings;

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

}  // namespace syslog_backend
