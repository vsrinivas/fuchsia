// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167.h"

#include <assert.h>
#include <fuchsia/hardware/gpio/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace board_mt8167 {

static pbus_dev_t rtc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "rtc";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_RTC_FALLBACK;
  return dev;
}();

zx_status_t Mt8167::Create(zx_device_t* parent) {
  pbus_protocol_t pbus;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  pdev_board_info_t board_info;
  status = pbus_get_board_info(&pbus, &board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Mt8167>(&ac, parent, &pbus, &board_info);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("mt8167s_ref", DEVICE_ADD_NON_BINDABLE);
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

int Mt8167::Thread() {
  if (SocInit() != ZX_OK) {
    zxlogf(ERROR, "SocInit() failed");
    return -1;
  }
  // Load protocol implementation drivers first.
  if (SysmemInit() != ZX_OK) {
    zxlogf(ERROR, "SysmemInit() failed");
    return -1;
  }
  if (PowerInit() != ZX_OK) {
    zxlogf(ERROR, "PowerInit() failed");
    return -1;
  }
  if (ClkInit() != ZX_OK) {
    zxlogf(ERROR, "ClkInit() failed");
    return -1;
  }
  if (GpioInit() != ZX_OK) {
    zxlogf(ERROR, "GpioInit() failed");
    return -1;
  }
  if (I2cInit() != ZX_OK) {
    zxlogf(ERROR, "I2cInit() failed");
    return -1;
  }
  // Then the platform device drivers.

  // eMMC
  if (Msdc0Init() != ZX_OK) {
    zxlogf(ERROR, "Msdc0Init() failed");
  }
  // SDIO
  if (Msdc2Init() != ZX_OK) {
    zxlogf(ERROR, "Msdc2Init() failed");
  }
  if (DisplayInit() != ZX_OK) {
    zxlogf(ERROR, "DisplayInit() failed");
  }
  if (ButtonsInit() != ZX_OK) {
    zxlogf(ERROR, "ButtonsInit() failed");
  }
  if (GpuInit() != ZX_OK) {
    zxlogf(ERROR, "GpuInit() failed");
  }
  if (UsbInit() != ZX_OK) {
    zxlogf(ERROR, "UsbInit() failed");
  }
  if (TouchInit() != ZX_OK) {
    zxlogf(ERROR, "TouchInit() failed");
  }
  if (ThermalInit() != ZX_OK) {
    zxlogf(ERROR, "ThermalInit() failed");
  }
  if (BacklightInit() != ZX_OK) {
    zxlogf(ERROR, "BacklightInit() failed");
  }
  if (AudioInit() != ZX_OK) {
    zxlogf(ERROR, "AudioInit() failed");
  }

  zx_status_t status = pbus_.DeviceAdd(&rtc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed for RTC - error %d", __func__, status);
    return -1;
  }

  return 0;
}

zx_status_t Mt8167::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Mt8167*>(arg)->Thread(); }, this,
      "mt8167-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Mt8167::DdkRelease() {
  free(usb_config_);
  delete this;
}

zx_status_t mt8167_bind(void* ctx, zx_device_t* parent) {
  return board_mt8167::Mt8167::Create(parent);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = mt8167_bind;
  return ops;
}();

}  // namespace board_mt8167

ZIRCON_DRIVER_BEGIN(mt8167, board_mt8167::driver_ops, "zircon", "0.1", 6)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_GOTO_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK, 0),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_MEDIATEK_8167S_REF), BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_CLEO), ZIRCON_DRIVER_END(mt8167)
