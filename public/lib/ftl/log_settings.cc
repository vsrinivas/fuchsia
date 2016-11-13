// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/log_settings.h"

#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_state.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

namespace ftl {

bool ParseLogSettings(const ftl::CommandLine& command_line,
                      LogSettings* out_settings) {
  LogSettings settings;

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
