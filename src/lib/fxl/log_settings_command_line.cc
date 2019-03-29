// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/log_settings_command_line.h"

#include "lib/fxl/command_line.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

namespace fxl {

bool ParseLogSettings(const fxl::CommandLine& command_line,
                      LogSettings* out_settings) {
  LogSettings settings = *out_settings;

  // --verbose=<level>
  // (always parse this even if --quiet is present)
  std::string verbosity;
  if (command_line.GetOptionValue("verbose", &verbosity)) {
    int level = 1;
    if (!verbosity.empty() &&
        (!fxl::StringToNumberWithError(verbosity, &level) || level < 0)) {
      FXL_LOG(ERROR) << "Error parsing --verbose option.";
      return false;
    }
    settings.min_log_level = -level;
  }

  // --quiet=<level>
  std::string quietness;
  if (command_line.GetOptionValue("quiet", &quietness)) {
    int level = 1;
    if (!quietness.empty() &&
        (!fxl::StringToNumberWithError(quietness, &level) || level < 0)) {
      FXL_LOG(ERROR) << "Error parsing --quiet option.";
      return false;
    }
    settings.min_log_level = level;
  }

  // --log-file=<file>
  std::string file;
  if (command_line.GetOptionValue("log-file", &file)) {
    settings.log_file = file;
  }

  *out_settings = settings;
  return true;
}

bool SetLogSettingsFromCommandLine(const fxl::CommandLine& command_line) {
  LogSettings settings;
  if (!ParseLogSettings(command_line, &settings))
    return false;
  SetLogSettings(settings);
  return true;
}

std::vector<std::string> LogSettingsToArgv(const LogSettings& settings) {
  std::vector<std::string> result;

  if (settings.min_log_level != LOG_INFO) {
    std::string arg;
    if (settings.min_log_level < 0) {
      arg = StringPrintf("--verbose=%d", -settings.min_log_level);
    } else {
      arg = StringPrintf("--quiet=%d", settings.min_log_level);
    }
    result.push_back(arg);
  }

  if (settings.log_file != "") {
    result.emplace_back(StringPrintf("--log-file=%s",
                                     settings.log_file.c_str()));
  }

  return result;
}

}  // namespace fxl
