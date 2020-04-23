// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "luis.h"

#include <zircon/status.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

namespace board_luis {

zx_status_t Luis::Create(void* ctx, zx_device_t* parent) {
  ddk::PBusProtocolClient pbus(parent);
  if (!pbus.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PBUS", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_board_info_t board_info;
  zx_status_t status = pbus.GetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get board info: %s", __func__, zx_status_get_string(status));
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Luis>(&ac, parent, pbus, board_info);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = board->DdkAdd("luis", DEVICE_ADD_NON_BINDABLE)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %s", __func__, zx_status_get_string(status));
    return status;
  }

  if ((status = board->Start()) != ZX_OK) {
    return status;
  }

  __UNUSED auto* dummy = board.release();
  return ZX_OK;
}

zx_status_t Luis::Start() {
  auto cb = [](void* arg) -> int { return reinterpret_cast<Luis*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "luis-start-thread");
  return thrd_status_to_zx_status(rc);
}

int Luis::Thread() {
  if (ClockInit() != ZX_OK) {
    zxlogf(ERROR, "%s: ClockInit() failed", __func__);
    return thrd_error;
  }

  if (GpioInit() != ZX_OK) {
    zxlogf(ERROR, "%s: GpioInit() failed", __func__);
    return thrd_error;
  }

  if (I2cInit() != ZX_OK) {
    zxlogf(ERROR, "%s: I2cInit() failed", __func__);
    return thrd_error;
  }

  if (PowerInit() != ZX_OK) {
    zxlogf(ERROR, "%s: PowerInit() failed", __func__);
    return thrd_error;
  }

  if (EmmcInit() != ZX_OK) {
    zxlogf(ERROR, "%s: EmmcInit() failed", __func__);
  }

  if (ThermalInit() != ZX_OK) {
    zxlogf(ERROR, "%s: ThermalInit() failed", __func__);
  }

  if (UsbInit() != ZX_OK) {
    zxlogf(ERROR, "%s: UsbInit() failed", __func__);
  }

  if (SdioInit() != ZX_OK) {
    zxlogf(ERROR, "%s: SdioInit() failed", __func__);
  }

  return 0;
}
}  // namespace board_luis

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = board_luis::Luis::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(luis, driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_LUIS),
ZIRCON_DRIVER_END(luis)
//clang-format on
