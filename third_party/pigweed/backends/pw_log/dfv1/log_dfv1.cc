// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <cstdarg>
#include <cstdio>

#include "pw_log/levels.h"
#include "pw_log_backend/log_backend.h"
#include "pw_string/string_builder.h"

namespace {

// This is an arbitrary size limit to printf logs.
constexpr size_t kPrintfBufferSize = 1024;

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

inline const char* LogLevelToString(int severity) {
  switch (severity) {
    case PW_LOG_LEVEL_ERROR:
      return "ERROR";
    case PW_LOG_LEVEL_WARN:
      return "WARN";
    case PW_LOG_LEVEL_INFO:
      return "INFO";
    case PW_LOG_LEVEL_DEBUG:
      return "DEBUG";
    default:
      return "UNKNOWN";
  }
}

}  // namespace

extern "C" void pw_Log(int level, unsigned int flags, const char* file_name, int line_number,
                       const char* message, ...) {
  if (flags & PW_LOG_FLAG_IGNORE) {
    return;
  }

  va_list args;
  va_start(args, message);

  if (flags & PW_LOG_FLAG_USE_PRINTF) {
    pw::StringBuffer<kPrintfBufferSize> buffer;
    buffer.Format("%s: [%s:%d] ", LogLevelToString(level), pw_log_ddk::BaseName(file_name),
                  line_number);
    buffer.FormatVaList(message, args);
    printf("%s\n", buffer.c_str());
    return;
  }

  zxlogvf_etc(LogLevelToDdkLog(level), nullptr, file_name, line_number, message, args);

  va_end(args);
}
