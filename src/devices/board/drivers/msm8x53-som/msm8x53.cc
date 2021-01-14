// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53.h"

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>

#include "src/devices/board/drivers/msm8x53-som/msm8x53_bind.h"

namespace board_msm8x53 {

zx_status_t Msm8x53::Create(zx_device_t* parent) {
  pbus_protocol_t pbus;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __func__, status);
    return status;
  }

  pdev_board_info_t board_info;
  status = pbus_get_board_info(&pbus, &board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed %d", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Msm8x53>(&ac, parent, &pbus, &board_info);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("msm8x53", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %d", __func__, status);
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

int Msm8x53::Thread() {
  if (GpioInit() != ZX_OK) {
    zxlogf(ERROR, "GpioInit() failed");
    return -1;
  }

  if (ClockInit() != ZX_OK) {
    zxlogf(ERROR, "ClockInit failed");
    return -1;
  }

  if (PowerInit() != ZX_OK) {
    zxlogf(ERROR, "PowerInit() failed");
    return -1;
  }

  if (PilInit() != ZX_OK) {
    zxlogf(ERROR, "PilInit() failed");
    return -1;
  }

  if (Sdc1Init() != ZX_OK) {
    zxlogf(ERROR, "Sdc1Init() failed");
    return -1;
  }
  return 0;
}

zx_status_t Msm8x53::Start() {
  auto cb = [](void* arg) -> int { return reinterpret_cast<Msm8x53*>(arg)->Thread(); };
  auto rc = thrd_create_with_name(&thread_, cb, this, "msm8x53-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Msm8x53::DdkRelease() { delete this; }

zx_status_t msm8x53_bind(void* ctx, zx_device_t* parent) {
  return board_msm8x53::Msm8x53::Create(parent);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = msm8x53_bind;
  return ops;
}();

}  // namespace board_msm8x53

ZIRCON_DRIVER(msm8x53, board_msm8x53::driver_ops, "zircon", "0.1");
