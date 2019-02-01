// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_LOG_SETTINGS_COMMAND_LINE_H_
#define LIB_FXL_LOG_SETTINGS_COMMAND_LINE_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/log_settings.h"

namespace fxl {

class CommandLine;
struct LogSettings;

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
FXL_EXPORT bool ParseLogSettings(const fxl::CommandLine& command_line,
                                 LogSettings* out_settings);

// Parses and applies log settings from standard command-line options.
// Returns false and leaves the active settings unchanged if there was an
// error parsing the options.
//
// See |ParseLogSettings| for syntax.
FXL_EXPORT bool SetLogSettingsFromCommandLine(
    const fxl::CommandLine& command_line);

}  // namespace fxl

#endif  // LIB_FXL_LOG_SETTINGS_COMMAND_LINE_H_
