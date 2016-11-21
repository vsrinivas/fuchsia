// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/log_settings.h"

#include <algorithm>

#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

namespace ftl {
namespace state {

// Defined in log_settings_state.cc.
extern LogSettings g_log_settings;

}  // namespace state

void SetLogSettings(const LogSettings& settings) {
  // Validate the new settings as we set them.
  state::g_log_settings.min_log_level =
      std::min(LOG_FATAL, settings.min_log_level);
}

LogSettings GetLogSettings() {
  return state::g_log_settings;
}

int GetMinLogLevel() {
  return state::g_log_settings.min_log_level;
}

bool ParseLogSettings(const ftl::CommandLine& command_line,
                      LogSettings* out_settings) {
  LogSettings settings = *out_settings;

  // --verbose=<level>
  // (always parse this even if --quiet is present)
  std::string verbosity;
  if (command_line.GetOptionValue("verbose", &verbosity)) {
    int level = 1;
    if (!verbosity.empty() &&
        (!ftl::StringToNumberWithError(verbosity, &level) || level < 0)) {
      FTL_LOG(ERROR) << "Error parsing --verbose option.";
      return false;
    }
    settings.min_log_level = -level;
  }

  // --quiet=<level>
  std::string quietness;
  if (command_line.GetOptionValue("quiet", &quietness)) {
    int level = 1;
    if (!quietness.empty() &&
        (!ftl::StringToNumberWithError(quietness, &level) || level < 0)) {
      FTL_LOG(ERROR) << "Error parsing --quiet option.";
      return false;
    }
    settings.min_log_level = level;
  }

  *out_settings = settings;
  return true;
}

bool SetLogSettingsFromCommandLine(const ftl::CommandLine& command_line) {
  LogSettings settings;
  if (!ParseLogSettings(command_line, &settings))
    return false;
  SetLogSettings(settings);
  return true;
}

}  // namespace ftl
