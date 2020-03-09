// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "c18.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

namespace board_c18 {

static pbus_dev_t rtc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "rtc";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_RTC_FALLBACK;
  return dev;
}();

zx_status_t C18::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<C18>(&ac, parent, &pbus);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("c18", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  // Start up our protocol helpers and platform devices.
  status = board->Start();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }
  return status;
}

int C18::Thread() {
  zx_status_t status;

  status = SocInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s:%d SocInit() failed.\n", __PRETTY_FUNCTION__, __LINE__);
    return -1;
  }

  status = GpioInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s:%d GpioInit() failed.\n", __PRETTY_FUNCTION__, __LINE__);
    return -1;
  }

  status = Msdc0Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s:%d Msdc0Init() failed.\n", __PRETTY_FUNCTION__, __LINE__);
    return -1;
  }

  status = SpiInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s:%d SpiInit() failed.\n", __PRETTY_FUNCTION__, __LINE__);
    return -1;
  }

  status = pbus_.DeviceAdd(&rtc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed for RTC - error %d\n", __func__, status);
    return -1;
  }

  return 0;
}

zx_status_t C18::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<C18*>(arg)->Thread(); }, this,
      "c18-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void C18::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = C18::Create;
  return ops;
}();

}  // namespace board_c18

ZIRCON_DRIVER_BEGIN(c18, board_c18::driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_C18), ZIRCON_DRIVER_END(c18)
