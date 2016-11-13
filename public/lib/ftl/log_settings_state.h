// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_LOG_SETTINGS_STATE_H_
#define LIB_FTL_LOG_SETTINGS_STATE_H_

#include "lib/ftl/log_level.h"

namespace ftl {

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
};

// Gets the active log settings for the current process.
void SetLogSettings(const LogSettings& settings);

// Sets the active log settings for the current process.
LogSettings GetLogSettings();

// Gets the minimum log level for the current process.
int GetMinLogLevel();

}  // namespace ftl

#endif  // LIB_FTL_LOG_SETTINGS_STATE_H_
