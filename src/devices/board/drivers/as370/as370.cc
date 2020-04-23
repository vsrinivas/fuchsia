// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370.h"

#include <zircon/status.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

namespace board_as370 {

zx_status_t As370::Create(void* ctx, zx_device_t* parent) {
  ddk::PBusProtocolClient pbus(parent);
  if (!pbus.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PBUS", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_board_info_t board_info;
  zx_status_t status = pbus.GetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get board info: %d", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<As370>(&ac, parent, pbus, board_info);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = board->DdkAdd("as370", DEVICE_ADD_NON_BINDABLE)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %s", __func__, zx_status_get_string(status));
    return status;
  }

  if ((status = board->Start()) != ZX_OK) {
    return status;
  }

  __UNUSED auto* dummy = board.release();
  return ZX_OK;
}

zx_status_t As370::Start() {
  auto cb = [](void* arg) -> int { return reinterpret_cast<As370*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "as370-start-thread");
  return thrd_status_to_zx_status(rc);
}

int As370::Thread() {
  auto status = GpioInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GpioInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  status = ClockInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ClkInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  status = I2cInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: I2cInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  if (UsbInit() != ZX_OK) {
    zxlogf(ERROR, "%s: UsbInit() failed", __func__);
  }

  if (AudioInit() != ZX_OK) {
    zxlogf(ERROR, "%s: AudioInit() failed", __func__);
    // In case of error report it and keep going.
  }

  if (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_VISALIA) {
    if (LightInit() != ZX_OK) {
      zxlogf(ERROR, "%s: LightInit() failed", __func__);
    }

    if (TouchInit() != ZX_OK) {
      zxlogf(ERROR, "%s: TouchInit() failed", __func__);
    }
  }

  if (NandInit() != ZX_OK) {
    zxlogf(ERROR, "%s: NandInit() failed", __func__);
  }

  if (PowerInit() != ZX_OK) {
    zxlogf(ERROR, "%s: PowerInit() failed", __func__);
    // In case of error report it and keep going.
  }

  if (ThermalInit() != ZX_OK) {
    zxlogf(ERROR, "%s: ThermalInit() failed", __func__);
  }

  if (SdioInit() != ZX_OK) {
    zxlogf(ERROR, "%s: SdioInit() failed", __func__);
  }

  return 0;
}
}  // namespace board_as370

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = board_as370::As370::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(as370, driver_ops, "zircon", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_GOTO_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS, 0),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_SYNAPTICS_AS370),
    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_VISALIA),
ZIRCON_DRIVER_END(as370)
//clang-format on
