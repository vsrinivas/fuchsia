// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_LOG_LEVEL_H_
#define LIB_FTL_LOG_LEVEL_H_

namespace ftl {

typedef int LogSeverity;

// Default log levels. Negative values can be used for verbose log levels.
constexpr LogSeverity LOG_INFO = 0;
constexpr LogSeverity LOG_WARNING = 1;
constexpr LogSeverity LOG_ERROR = 2;
constexpr LogSeverity LOG_FATAL = 3;
constexpr LogSeverity LOG_NUM_SEVERITIES = 4;

// LOG_DFATAL is LOG_FATAL in debug mode, ERROR in normal mode
#ifdef NDEBUG
const LogSeverity LOG_DFATAL = LOG_ERROR;
#else
const LogSeverity LOG_DFATAL = LOG_FATAL;
#endif

// Sets the log level. Anything at or above this level will be written to the
// log file/displayed to the user (if applicable). Anything below this level
// will be silently ignored. The log level defaults to 0.
//
// Note that log messages for FTL_VLOG(x) (from lib/ftl/logging.h) are logged at
// level -x, so setting the min log level to negative values enables verbose
// logging.
void SetMinLogLevel(int level);

// Gets the current log level.
int GetMinLogLevel();

}  // namespace ftl

#endif  // LIB_FTL_LOG_LEVEL_H_
