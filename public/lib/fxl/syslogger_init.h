// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_SYSLOGGER_INIT_H_
#define LIB_FXL_SYSLOGGER_INIT_H_

#include "lib/fxl/command_line.h"
#include "lib/fxl/fxl_export.h"
#include "lib/syslog/cpp/logger.h"

namespace fxl {

// Parses log settings from standard command-line options.
//
// Recognizes the following options:
//   --verbose         : sets |min_log_level| to -1
//   --verbose=<level> : sets |min_log_level| to -level
//   --quiet           : sets |min_log_level| to +1 (FX_LOG_INFO)
//   --quiet=<level>   : sets |min_log_level| to +level
//   --log-file=<file> : sets |log_file| to file, uses default output if empty
//
// Quiet supersedes verbose if both are specified.
//
// Returns error string if there was an error. |out_settings| might be partially
// updated depending on which commandline option failed.
// Otherwise updates |out_settings| with any values which were overridden by the
// command-line.
FXL_EXPORT std::string ParseLoggerSettings(const fxl::CommandLine& command_line,
                                           syslog::LogSettings* out_settings);

// Parses and creates logger from standard command-line options and provided
// tags.
// Returns zx_status_t if it is not able to create logger.
//
// Recognizes the following options:
//   --verbose         : sets |min_log_level| to -1
//   --verbose=<level> : sets |min_log_level| to -level
//   --quiet           : sets |min_log_level| to +1 (FX_LOG_INFO)
//   --quiet=<level>   : sets |min_log_level| to +level
//   --log-file=<file> : sets |log_file| to file, uses default output if empty
//
// Quiet supersedes verbose if both are specified.
FXL_EXPORT zx_status_t
InitLoggerFromCommandLine(const fxl::CommandLine& command_line,
                          std::initializer_list<std::string> tags);

// Parses and creates logger from standard command-line options.
// Returns zx_status_t if it is not able to create logger.
//
// Recognizes the following options:
//   --verbose         : sets |min_log_level| to -1
//   --verbose=<level> : sets |min_log_level| to -level
//   --quiet           : sets |min_log_level| to +1 (FX_LOG_INFO)
//   --quiet=<level>   : sets |min_log_level| to +level
//   --log-file=<file> : sets |log_file| to file, uses default output if empty
//
// Quiet supersedes verbose if both are specified.
FXL_EXPORT zx_status_t
InitLoggerFromCommandLine(const fxl::CommandLine& command_line);

}  // namespace fxl

#endif  // LIB_FXL_SYSLOGGER_INIT_H_
