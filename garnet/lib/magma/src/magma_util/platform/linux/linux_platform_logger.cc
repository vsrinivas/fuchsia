// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>

#include "platform_logger.h"

namespace magma {

void PlatformLogger::Log(LogLevel level, const char* msg, ...) {
  switch (level) {
    case PlatformLogger::LOG_ERROR:
      printf("[ERROR] ");
      break;
    case PlatformLogger::LOG_WARNING:
      printf("[WARNING] ");
      break;
    case PlatformLogger::LOG_INFO:
      printf("[INFO] ");
      break;
  }
  va_list args;
  va_start(args, msg);
  vprintf(msg, args);
  va_end(args);
  printf("\n");
}

}  // namespace magma
