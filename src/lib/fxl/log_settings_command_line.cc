// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/log_settings_command_line.h"

#include <lib/syslog/cpp/macros.h>

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

  // --severity=<TRACE|DEBUG|INFO|WARNING|ERROR|FATAL>
  std::string severity;
  if (command_line.GetOptionValue("severity", &severity)) {
    syslog::LogSeverity level;
    if (severity == "TRACE") {
      level = syslog::LOG_TRACE;
    } else if (severity == "DEBUG") {
      level = syslog::LOG_DEBUG;
    } else if (severity == "INFO") {
      level = syslog::LOG_INFO;
    } else if (severity == "WARNING") {
      level = syslog::LOG_WARNING;
    } else if (severity == "ERROR") {
      level = syslog::LOG_ERROR;
    } else if (severity == "FATAL") {
      level = syslog::LOG_FATAL;
    } else {
      FX_LOGS(ERROR) << "Error parsing --severity option:" << severity;
      return false;
    }

    settings.min_log_level = level;
  }

  // --verbose=<level>
  // (always parse this even if --quiet is present)
  // Errors if --severity is present.
  std::string verbosity;
  if (command_line.GetOptionValue("verbose", &verbosity)) {
    if (!severity.empty()) {
      FX_LOGS(ERROR) << "Setting both --severity and --verbose is not allowed.";
      return false;
    }

    int level = 1;
    if (!verbosity.empty() && (!fxl::StringToNumberWithError(verbosity, &level) || level < 0)) {
      FX_LOGS(ERROR) << "Error parsing --verbose option: " << verbosity;
      return false;
    }

    // verbosity scale sits in the interstitial space between INFO and DEBUG
    settings.min_log_level = std::max<syslog::LogSeverity>(
        syslog::LOG_INFO - static_cast<syslog::LogSeverity>(level * syslog::LogVerbosityStepSize),
        syslog::LOG_DEBUG + 1);
  }
#ifndef __Fuchsia__
  // --log-file=<file>
  std::string file;
  if (command_line.GetOptionValue("log-file", &file)) {
    settings.log_file = file;
  }
#endif
  // --quiet=<level>
  // Errors out if --severity is present.
  std::string quietness;
  if (command_line.GetOptionValue("quiet", &quietness)) {
    if (!severity.empty()) {
      FX_LOGS(ERROR) << "Setting both --severity and --quiet is not allowed.";
      return false;
    }

    int level = 1;
    if (!quietness.empty() && (!fxl::StringToNumberWithError(quietness, &level) || level < 0)) {
      FX_LOGS(ERROR) << "Error parsing --quiet option: " << quietness;
      return false;
    }
    // Max quiet steps from INFO > WARN > ERROR > FATAL
    if (level > 3) {
      level = 3;
    }
    settings.min_log_level =
        syslog::LOG_INFO + static_cast<syslog::LogSeverity>(level * syslog::LogSeverityStepSize);
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
    if (settings.min_log_level < syslog::LOG_INFO && settings.min_log_level > syslog::LOG_DEBUG) {
      arg = StringPrintf("--verbose=%d", (syslog::LOG_INFO - settings.min_log_level));
    } else if (settings.min_log_level == syslog::LOG_TRACE) {
      arg = "--severity=TRACE";
    } else if (settings.min_log_level == syslog::LOG_DEBUG) {
      arg = "--severity=DEBUG";
    } else if (settings.min_log_level == syslog::LOG_WARNING) {
      arg = "--severity=WARNING";
    } else if (settings.min_log_level == syslog::LOG_ERROR) {
      arg = "--severity=ERROR";
    } else {
      arg = "--severity=FATAL";
    }
    result.push_back(arg);
  }

  return result;
}  // namespace fxl

}  // namespace fxl
