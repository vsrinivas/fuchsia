// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <iostream>

#include "src/lib/fxl/logging_backend.h"

namespace fxl_logging_backend {

namespace {

// It's OK to keep global state here even though this file is in a source_set because on host
// we don't use shared libraries.
fxl::LogSettings g_log_settings;

}  // namespace

void SetSettings(const fxl::LogSettings& settings) {
  g_log_settings.min_log_level = std::min(fxl::LOG_FATAL, settings.min_log_level);

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

void SetSettings(const fxl::LogSettings& settings, const std::initializer_list<std::string>& tags) {
  // Global tags aren't supported on host.
  SetSettings(settings);
}

void SetTags(const std::initializer_list<std::string>& tags) {
  // Global tags aren't supported on host.
}

fxl::LogSeverity GetMinLogLevel() { return g_log_settings.min_log_level; }

}  // namespace fxl_logging_backend
