// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>

#include "platform_logger.h"

namespace magma {

bool PlatformLogger::IsInitialized() { return true; }

bool PlatformLogger::Initialize(std::unique_ptr<PlatformHandle> handle) { return true; }

static void print_level(PlatformLogger::LogLevel level) {
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
}

void PlatformLogger::LogVa(LogLevel level, const char* msg, va_list args) {
  print_level(level);
  vprintf(msg, args);
  printf("\n");
}

}  // namespace magma
