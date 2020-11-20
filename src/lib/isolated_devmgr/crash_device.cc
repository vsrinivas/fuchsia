// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>

#include "src/lib/isolated_devmgr/crash-device-bind.h"

namespace crash {

class CrashDevice;

using CrashDeviceType = ddk::Device<CrashDevice, ddk::Openable>;

class CrashDevice : public CrashDeviceType {
 public:
  explicit CrashDevice(zx_device_t* parent) : CrashDeviceType(parent) {}

  zx_status_t DdkOpen(zx_device_t** out, uint32_t flags) {
    zxlogf(INFO, "Crash-device open, will crash on purpose!");
    // We crash using a bad access here instead of just asserting false because there are some bots
    // in CQ that are looking for the ASSERT FAILED message to mark runs as failed.
    __builtin_abort();
    return ZX_OK;
  }

  void DdkRelease() { delete this; }

  static zx_status_t Create(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "CrashDevice::Create");
    auto dev = std::unique_ptr<CrashDevice>(new CrashDevice(parent));
    zx_status_t status = dev->DdkAdd("crash-device");
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
    } else {
      // devmgr owns the memory now
      __UNUSED auto* ptr = dev.release();
    }
    return status;
  }
};

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CrashDevice::Create;
  return ops;
}();

}  // namespace crash

ZIRCON_DRIVER(crash_device, crash::driver_ops, "fuchsia", "0.1")
