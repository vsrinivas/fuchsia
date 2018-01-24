// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/fxl/log_settings.h"

#include "host_device.h"

namespace {

// TODO(armansito): Switch from FXL logging to DDK logging. We'll want to first
// come up with a system within btlib so that logging works both in a devmgr
// environment and outside (e.g. tools and unit tests) (NET-357).

#define BT_DEBUG 0
#if BT_DEBUG
constexpr fxl::LogSeverity kLogLevel = -5;
#else
constexpr fxl::LogSeverity kLogLevel = fxl::LOG_ERROR;
#endif

}  // namespace

extern "C" zx_status_t bthost_bind(void* ctx, zx_device_t* device) {
  fxl::LogSettings log_settings;
  log_settings.min_log_level = kLogLevel;
  fxl::SetLogSettings(log_settings);

  auto dev = std::make_unique<bthost::HostDevice>(device);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for |dev|.
    dev.release();
  }

  return status;
}
