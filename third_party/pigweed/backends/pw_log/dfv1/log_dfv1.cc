// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <cstdarg>
#include <cstdio>

#include "pw_log/levels.h"
#include "pw_log_backend/log_backend.h"

namespace {

inline fx_log_severity_t LogLevelToDdkLog(int level) {
  switch (level) {
    case PW_LOG_LEVEL_DEBUG:
      return DDK_LOG_DEBUG;
    case PW_LOG_LEVEL_INFO:
      return DDK_LOG_INFO;
    case PW_LOG_LEVEL_WARN:
      return DDK_LOG_WARNING;
    case PW_LOG_LEVEL_ERROR:
      return DDK_LOG_ERROR;
    case PW_LOG_LEVEL_CRITICAL:
      return DDK_LOG_ERROR;
    default:
      return DDK_LOG_INFO;
  }
}

}  // namespace

extern "C" void pw_Log(int level, unsigned int flags, const char* file_name, int line_number,
                       const char* message, ...) {
  va_list args;
  va_start(args, message);
  zxlogvf_etc(LogLevelToDdkLog(level), nullptr, file_name, line_number, message, args);
  va_end(args);
}
