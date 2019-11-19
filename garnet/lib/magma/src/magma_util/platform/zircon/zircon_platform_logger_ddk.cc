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

void PlatformLogger::Log(LogLevel level, const char* msg, ...) {
  char buffer[512];
  va_list ap;
  va_start(ap, msg);
  vsnprintf(buffer, sizeof(buffer), msg, ap);
  va_end(ap);

  const char* fmt = "%s";
  switch (level) {
    case LOG_ERROR:
      zxlogf(ERROR, fmt, buffer);
      return;
    case LOG_WARNING:
      zxlogf(WARN, fmt, buffer);
      return;
    case LOG_INFO:
      zxlogf(INFO, fmt, buffer);
      return;
  }
}

}  // namespace magma
