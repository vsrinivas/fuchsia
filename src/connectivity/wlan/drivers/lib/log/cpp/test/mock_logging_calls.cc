// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/syslog/logger.h>
#include <zircon/types.h>

#include "log_test.h"

using wlan::drivers::LogTest;

LogTest* LogTest::instance_ = nullptr;

extern "C" bool driver_log_severity_enabled_internal(const zx_driver_t* drv,
                                                     fx_log_severity_t flag) {
  return true;
}

extern "C" void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                      const char* tag, const char* file, int line, const char* fmt,
                                      va_list args) {
  LogTest::GetInstance().ZxlogfEtcOverride(flag, tag);
}

extern "C" void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                     const char* tag, const char* file, int line, const char* fmt,
                                     ...) {
  va_list args;
  va_start(args, fmt);
  driver_logvf_internal(drv, flag, tag, file, line, fmt, args);
  va_end(args);
}

__WEAK zx_driver_rec __zircon_driver_rec__ = {
    .ops = {},
    .driver = {},
};
