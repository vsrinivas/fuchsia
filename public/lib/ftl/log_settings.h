// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_LOG_SETTINGS_H_
#define LIB_FTL_LOG_SETTINGS_H_

#include "lib/ftl/log_level.h"

#include <string>

namespace ftl {

class CommandLine;

// Settings which control the behavior of FTL logging.
struct LogSettings {
  // The minimum logging level.
  // Anything at or above this level will be logged (if applicable).
  // Anything below this level will be silently ignored.
  //
  // The log level defaults to 0 (LOG_INFO).
  //
  // Log messages for FTL_VLOG(x) (from lib/ftl/logging.h) are logged
  // at level -x, so setting the min log level to negative values enables
  // verbose logging.
  LogSeverity min_log_level = LOG_INFO;

  // The name of a file to which the log should be written.
  // When non-empty, the previous log output is closed and logging is
  // redirected to the specified file.  It is not possible to revert to
  // the previous log output through this interface.
  std::string log_file;
};

// Gets the active log settings for the current process.
void SetLogSettings(const LogSettings& settings);

// Sets the active log settings for the current process.
LogSettings GetLogSettings();

// Gets the minimum log level for the current process.
int GetMinLogLevel();

// Parses log settings from standard command-line options.
//
// Recognizes the following options:
//   --verbose         : sets |min_log_level| to -1
//   --verbose=<level> : sets |min_log_level| to -level
//   --quiet           : sets |min_log_level| to +1 (LOG_WARNING)
//   --quiet=<level>   : sets |min_log_level| to +level
//   --log-file=<file> : sets |log_file| to file, uses default output if empty
//
// Quiet supersedes verbose if both are specified.
//
// Returns false and leaves |out_settings| unchanged if there was an
// error parsing the options.  Otherwise updates |out_settings| with any
// values which were overridden by the command-line.
bool ParseLogSettings(const ftl::CommandLine& command_line,
                      LogSettings* out_settings);

// Parses and applies log settings from standard command-line options.
// Returns false and leaves the active settings unchanged if there was an
// error parsing the options.
//
// See |ParseLogSettings| for syntax.
bool SetLogSettingsFromCommandLine(const ftl::CommandLine& command_line);

}  // namespace ftl

#endif  // LIB_FTL_LOG_SETTINGS_H_
