// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syslogger_init.h"

#include <fcntl.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/syslog/cpp/logger.h"

namespace fxl {

std::string ParseLoggerSettings(const fxl::CommandLine& command_line,
                                syslog::LogSettings* out_settings) {
  fx_log_severity_t severity = out_settings->severity;

  // --verbose=<level>
  // (always parse this even if --quiet is present)
  std::string verbosity;
  if (command_line.GetOptionValue("verbose", &verbosity)) {
    int level = 1;
    if (!verbosity.empty() &&
        (!fxl::StringToNumberWithError(verbosity, &level) || level < 0)) {
      return "Error parsing --verbose option. Using default logging level";
    }
    severity = -level;
  }

  // --quiet=<level>
  std::string quietness;
  if (command_line.GetOptionValue("quiet", &quietness)) {
    int level = FX_LOG_INFO;
    if (!quietness.empty() &&
        (!fxl::StringToNumberWithError(quietness, &level) || level < 0)) {
      return "Error parsing --quiet option. Using default logging level";
    }
    severity = level;
  }
  out_settings->severity = severity;

  // --log-file=<file>
  std::string file;
  if (command_line.GetOptionValue("log-file", &file)) {
    int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
      std::ostringstream os;
      os << "Could not open log file: " << file << " (" << strerror(errno)
         << "). Logging to default.";
      return os.str();
    }
    out_settings->fd = fd;
  }
  return "";
}

zx_status_t InitLoggerFromCommandLine(const fxl::CommandLine& command_line,
                                      std::initializer_list<std::string> tags) {
  syslog::LogSettings settings = {FX_LOG_INFO, -1};
  std::string error = ParseLoggerSettings(command_line, &settings);
  auto status = syslog::InitLogger(settings, tags);
  if (status == ZX_OK && !error.empty()) {
    FX_LOGST(ERROR, "logger_init_error") << error;
  } else if (status != ZX_OK && settings.fd != -1) {
    close(settings.fd);
  }
  return status;
}

zx_status_t InitLoggerFromCommandLine(const fxl::CommandLine& command_line) {
  return InitLoggerFromCommandLine(command_line, {});
}

}  // namespace fxl
