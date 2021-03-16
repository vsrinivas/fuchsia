// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8998.h"

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/board/drivers/msm8998/msm8998_bind.h"

namespace board_msm8998 {

zx_status_t Msm8998::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __func__, status);
    return status;
  }

  auto board = std::make_unique<Msm8998>(parent, &pbus);
  status = board->DdkAdd("msm8998", DEVICE_ADD_NON_BINDABLE);
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

int Msm8998::Thread() {
  /*
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
  */
  return 0;
}

zx_status_t Msm8998::Start() {
  auto cb = [](void* arg) -> int { return reinterpret_cast<Msm8998*>(arg)->Thread(); };
  auto rc = thrd_create_with_name(&thread_, cb, this, "msm8998-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Msm8998::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Msm8998::Create;
  return ops;
}();

}  // namespace board_msm8998

ZIRCON_DRIVER(msm8998, board_msm8998::driver_ops, "zircon", "0.1");
