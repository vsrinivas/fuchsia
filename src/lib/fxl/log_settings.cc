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
#include <lib/syslog/global.h>
#include <lib/zx/process.h>
#endif

namespace fxl {
namespace state {

// Defined in log_settings_state.cc.
extern LogSettings g_log_settings;

}  // namespace state

#if defined(__Fuchsia__)
namespace {

void SetSyslogSettings(int fd, const std::initializer_list<std::string>& tags) {
  const char* ctags[FX_LOG_MAX_TAGS];
  int i = 0;
  for (auto& tag : tags) {
    ctags[i++] = tag.c_str();
  }
  fx_logger_config_t config = {.min_severity = state::g_log_settings.min_log_level,
                               .console_fd = fd,
                               .log_service_channel = ZX_HANDLE_INVALID,
                               .tags = ctags,
                               .num_tags = tags.size()};
  fx_log_reconfigure(&config);
}

}  // namespace
#endif  // defined(__Fuchsia__)

void SetLogSettings(const LogSettings& settings) {
#ifdef __Fuchsia__
  char process_name[ZX_MAX_NAME_LEN] = "";
  zx_status_t status =
      zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  if (status != ZX_OK)
    process_name[0] = '\0';
  SetLogSettings(settings, {process_name});
#else
  SetLogSettings(settings, {});
#endif
}

void SetLogSettings(const LogSettings& settings, const std::initializer_list<std::string>& tags) {
  // Validate the new settings as we set them.
  state::g_log_settings.min_log_level = std::min(LOG_FATAL, settings.min_log_level);

#ifdef __Fuchsia__
  _FX_LOG_SET_SEVERITY(state::g_log_settings.min_log_level);
  SetSyslogSettings(-1, tags);
#endif

  if (state::g_log_settings.log_file != settings.log_file) {
    if (!settings.log_file.empty()) {
      int fd = open(settings.log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND);
      if (fd < 0) {
        std::cerr << "Could not open log file: " << settings.log_file << " (" << strerror(errno)
                  << ")" << std::endl;
      } else {
#ifdef __Fuchsia__
        SetSyslogSettings(fd, tags);
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

void SetLogTags(const std::initializer_list<std::string>& tags) {
#ifdef __Fuchsia__
  SetSyslogSettings(-1, tags);
#endif
}

LogSettings GetLogSettings() { return state::g_log_settings; }

int GetMinLogLevel() { return std::min(state::g_log_settings.min_log_level, LOG_FATAL); }

}  // namespace fxl
