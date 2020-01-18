// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>

#include <ddk/debug.h>

#include "platform_logger.h"

namespace magma {

bool PlatformLogger::IsInitialized() { return true; }

bool PlatformLogger::Initialize(std::unique_ptr<PlatformHandle> handle) { return true; }

static void log_cstr(PlatformLogger::LogLevel level, const char* cstr) {
  const char* fmt = "%s\n";
  switch (level) {
    case PlatformLogger::LOG_ERROR:
      zxlogf(ERROR, fmt, cstr);
      return;
    case PlatformLogger::LOG_WARNING:
      zxlogf(WARN, fmt, cstr);
      return;
    case PlatformLogger::LOG_INFO:
      zxlogf(INFO, fmt, cstr);
      return;
  }
}

void PlatformLogger::LogVa(LogLevel level, const char* msg, va_list args) {
  char buffer[512];
  vsnprintf(buffer, sizeof(buffer), msg, args);
  log_cstr(level, buffer);
}

}  // namespace magma
