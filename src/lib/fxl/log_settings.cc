// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/log_settings.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <iostream>

#include "lib/fxl/logging.h"

namespace fxl {
namespace state {

// Defined in log_settings_state.cc.
extern LogSettings g_log_settings;

}  // namespace state

void SetLogSettings(const LogSettings& settings) {
  // Validate the new settings as we set them.
  state::g_log_settings.min_log_level =
      std::min(LOG_FATAL, settings.min_log_level);

  if (state::g_log_settings.log_file != settings.log_file) {
    if (!settings.log_file.empty()) {
      // Redirect stderr to file.
      int fd = open(settings.log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND);
      if (fd < 0) {
        std::cerr << "Could not open log file: " << settings.log_file << " ("
                  << strerror(errno) << ")" << std::endl;
      } else {
        if (dup2(fd, STDERR_FILENO) < 0)
          std::cerr << "Could not set stderr to log file: " << settings.log_file
                    << " (" << strerror(errno) << ")" << std::endl;
        else
          state::g_log_settings.log_file = settings.log_file;
        close(fd);
      }
    }
  }
}

LogSettings GetLogSettings() { return state::g_log_settings; }

int GetMinLogLevel() {
  return std::min(state::g_log_settings.min_log_level, LOG_FATAL);
}

}  // namespace fxl
