// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_LOG_SETTINGS_H_
#define SRC_LIB_FXL_LOG_SETTINGS_H_

#include <string>

#include "src/lib/fxl/log_level.h"

namespace fxl {

// Settings which control the behavior of FXL logging.
struct LogSettings {
  // The minimum logging level.
  // Anything at or above this level will be logged (if applicable).
  // Anything below this level will be silently ignored.
  //
  // The log level defaults to 0 (LOG_INFO).
  //
  // Log messages for FXL_VLOG(x) (from lib/fxl/logging.h) are logged
  // at level -x, so setting the min log level to negative values enables
  // verbose logging.
  LogSeverity min_log_level = LOG_INFO;

  // The name of a file to which the log should be written.
  // When non-empty, the previous log output is closed and logging is
  // redirected to the specified file.  It is not possible to revert to
  // the previous log output through this interface.
  std::string log_file;
};

// Sets the active log settings for the current process.
void SetLogSettings(const LogSettings& settings);

// Sets the active log settings and tags for the current process. |tags| is not
// used on host.
void SetLogSettings(const LogSettings& settings, const std::initializer_list<std::string>& tags);

// Sets the log tags without modifying the settings. This is ignored on the host.
void SetLogTags(const std::initializer_list<std::string>& tags);

// Gets the minimum log level for the current process. Never returns a value
// higher than LOG_FATAL.
LogSeverity GetMinLogLevel();

}  // namespace fxl

#endif  // SRC_LIB_FXL_LOG_SETTINGS_H_
