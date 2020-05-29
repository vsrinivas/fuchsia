// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_LOG_SETTINGS_COMMAND_LINE_H_
#define SRC_LIB_FXL_LOG_SETTINGS_COMMAND_LINE_H_

#include <lib/syslog/cpp/log_settings.h>

#include <string>
#include <vector>

#include "src/lib/fxl/fxl_export.h"

namespace fxl {

class CommandLine;

// Parses log settings from standard command-line options.
//
// Recognizes the following options:
//   --severity=<TRACE|DEBUG|INFO|WARNING|ERROR|FATAL> : sets |min_log_level| to indicated severity
//   --verbose         : sets |min_log_level| to (LOG_INFO - 1)
//   --verbose=<level> : sets |min_log_level| incrementally lower than INFO
//   --quiet           : sets |min_log_level| to LOG_WARNING
//   --quiet=<level>   : sets |min_log_level| incrementally higher than INFO
//   --log-file=<file> : sets |log_file| to file, uses default output if empty
//
// Quiet supersedes verbose if both are specified.
//
// Returns false and leaves |out_settings| unchanged if there was an
// error parsing the options.  Otherwise updates |out_settings| with any
// values which were overridden by the command-line.
bool ParseLogSettings(const fxl::CommandLine& command_line, syslog::LogSettings* out_settings);

// Parses and applies log settings from standard command-line options.
// Returns false and leaves the active settings unchanged if there was an
// error parsing the options.
//
// See |ParseLogSettings| for syntax.
bool SetLogSettingsFromCommandLine(const fxl::CommandLine& command_line);

// Similar to the method above but uses the given list of tags instead of
// the default which is the process name. On host |tags| is ignored.
bool SetLogSettingsFromCommandLine(const fxl::CommandLine& command_line,
                                   const std::initializer_list<std::string>& tags);

// Do the opposite of |ParseLogSettings()|: Convert |settings| to the
// command line arguments to pass to a program. The result is empty if
// |settings| is the default.
std::vector<std::string> LogSettingsToArgv(const syslog::LogSettings& settings);

}  // namespace fxl

#endif  // SRC_LIB_FXL_LOG_SETTINGS_COMMAND_LINE_H_
