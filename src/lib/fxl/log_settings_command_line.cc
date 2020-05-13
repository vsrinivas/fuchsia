// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/log_settings_command_line.h"

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fxl {

bool ParseLogSettings(const fxl::CommandLine& command_line, syslog::LogSettings* out_settings) {
  syslog::LogSettings settings = *out_settings;

  // Don't clobber existing settings, but ensure min log level is initialized.
  // Note that legacy INFO level is also 0.
  if (settings.min_log_level == 0) {
    settings.min_log_level = syslog::DefaultLogLevel;
  }

  // --verbose=<level>
  // (always parse this even if --quiet is present)
  std::string verbosity;
  if (command_line.GetOptionValue("verbose", &verbosity)) {
    int level = 1;
    if (!verbosity.empty() && (!fxl::StringToNumberWithError(verbosity, &level) || level < 0)) {
      FX_LOGS(ERROR) << "Error parsing --verbose option: " << verbosity;
      return false;
    }

    // verbosity scale sits in the interstitial space between INFO and DEBUG
    settings.min_log_level = std::max(int(syslog::LOG_DEBUG) + 1,
                                      syslog::LOG_INFO - (level * syslog::LogVerbosityStepSize));
  }

  // --quiet=<level>
  std::string quietness;
  if (command_line.GetOptionValue("quiet", &quietness)) {
    int level = 1;
    if (!quietness.empty() && (!fxl::StringToNumberWithError(quietness, &level) || level < 0)) {
      FX_LOGS(ERROR) << "Error parsing --quiet option: " << quietness;
      return false;
    }
    // Max quiet steps from INFO > WARN > ERROR > FATAL
    if (level > 3) {
      level = 3;
    }
    settings.min_log_level = syslog::LOG_INFO + (level * syslog::LogSeverityStepSize);
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
  syslog::LogSettings settings;
  if (!ParseLogSettings(command_line, &settings))
    return false;
  SetLogSettings(settings);
  return true;
}

bool SetLogSettingsFromCommandLine(const fxl::CommandLine& command_line,
                                   const std::initializer_list<std::string>& tags) {
  syslog::LogSettings settings;
  if (!ParseLogSettings(command_line, &settings))
    return false;
  SetLogSettings(settings, tags);
  return true;
}

std::vector<std::string> LogSettingsToArgv(const syslog::LogSettings& settings) {
  std::vector<std::string> result;

  if (settings.min_log_level != syslog::LOG_INFO) {
    std::string arg;
    if (settings.min_log_level < syslog::LOG_INFO) {
      arg = StringPrintf("--verbose=%d", (syslog::LOG_INFO - settings.min_log_level));
    } else {
      arg = StringPrintf(
          "--quiet=%d", -(syslog::LOG_INFO - settings.min_log_level) / syslog::LogSeverityStepSize);
    }
    result.push_back(arg);
  }

  if (settings.log_file != "") {
    result.emplace_back(StringPrintf("--log-file=%s", settings.log_file.c_str()));
  }

  return result;
}

}  // namespace fxl
