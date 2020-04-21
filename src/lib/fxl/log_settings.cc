// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/log_settings.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>

#include "src/lib/fxl/logging.h"

#ifdef __Fuchsia__
#include <lib/zx/process.h>

#include "src/lib/syslog/cpp/logger.h"
#endif

namespace fxl {
namespace state {

// Defined in log_settings_state.cc.
extern LogSettings g_log_settings;

}  // namespace state

void SetLogSettings(const LogSettings& settings) {
  // Validate the new settings as we set them.
  state::g_log_settings.min_log_level = std::min(LOG_FATAL, settings.min_log_level);

#ifdef __Fuchsia__
  _FX_LOG_SET_SEVERITY(state::g_log_settings.min_log_level);
#endif

  if (state::g_log_settings.log_file != settings.log_file) {
    if (!settings.log_file.empty()) {
      int fd = open(settings.log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND);
      if (fd < 0) {
        std::cerr << "Could not open log file: " << settings.log_file << " (" << strerror(errno)
                  << ")" << std::endl;
      } else {
#ifdef __Fuchsia__
        char process_name[ZX_MAX_NAME_LEN] = "";
        zx_status_t status =
            zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
        if (status != ZX_OK)
          process_name[0] = '\0';
        syslog::SetSettings({.severity = state::g_log_settings.min_log_level, .fd = fd},
                            {process_name});
        state::g_log_settings.log_file = settings.log_file;
#else
        // Redirect stderr to file.
        if (dup2(fd, STDERR_FILENO) < 0)
          std::cerr << "Could not set stderr to log file: " << settings.log_file << " ("
                    << strerror(errno) << ")" << std::endl;
        else
          state::g_log_settings.log_file = settings.log_file;
        close(fd);
#endif
      }
    }
  }
}

LogSettings GetLogSettings() { return state::g_log_settings; }

int GetMinLogLevel() { return std::min(state::g_log_settings.min_log_level, LOG_FATAL); }

}  // namespace fxl
