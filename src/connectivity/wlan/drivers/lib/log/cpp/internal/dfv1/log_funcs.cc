// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <wlan/drivers/log_instance.h>

extern "C" void wlan_drivers_log_with_severity(fx_log_severity_t severity, uint32_t filter,
                                               const char* tag, const char* file, int line,
                                               const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  switch (severity) {
    case DDK_LOG_ERROR:
    case DDK_LOG_WARNING:
    case DDK_LOG_INFO:
      zxlogvf_etc(severity, tag, file, line, fmt, args);
      break;
    case DDK_LOG_DEBUG:
    case DDK_LOG_TRACE:
      if (unlikely(wlan::drivers::log::Instance::IsFilterOn(filter))) {
        zxlogvf_etc(severity, tag, file, line, fmt, args);
      }
      break;
    default:
      zxlogf(WARNING, "Unrecognized log severity: %u. Logging message with WARNING level instead.",
             severity);
      zxlogvf_etc(DDK_LOG_WARNING, tag, file, line, fmt, args);
      break;
  }

  va_end(args);
}
