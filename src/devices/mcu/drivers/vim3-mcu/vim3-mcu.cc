// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim3-mcu.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/i2c.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <iterator>

#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "src/devices/mcu/drivers/vim3-mcu/vim3_mcu_bind.h"

namespace stm {
zx_status_t StmMcu::Create(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get ZX_PROTOCOL_I2C");
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<StmMcu> device(new (&ac) StmMcu(parent, std::move(i2c)));
  if (!ac.check()) {
    zxlogf(ERROR, "StmMcu alloc failed");
    return ZX_ERR_NO_MEMORY;
  }

  device->Init();

  if (zx_status_t status = device->DdkAdd("vim3-mcu"); status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed");
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}
void StmMcu::ShutDown() {}

void StmMcu::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}
void StmMcu::DdkRelease() { delete this; }

void StmMcu::Init() {
  SetFanLevel(FL1);

  const uint8_t wol_reset_enable[] = {STM_MCU_REG_BOOT_EN_WOL, STM_MCU_REG_EN_WOL_RESET_ENABLE};
  zx_status_t status = i2c_.WriteSync(wol_reset_enable, countof(wol_reset_enable));
  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to enable WOL: %d", status);
  }
}

zx_status_t StmMcu::SetFanLevel(FanLevel level) {
  // TODO fxbug.dev/56400: rsubr clean this up to implement outward intf for rd/wr
  // for now just turning on the fan to prevent soc from overheating
  fbl::AutoLock lock(&i2c_lock_);
  uint8_t cmd[] = {STM_MCU_REG_CMD_FAN_STATUS_CTRL_REG, static_cast<uint8_t>(level)};
  auto status = i2c_.WriteSync(cmd, sizeof(cmd));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not set the fan level: %d", status);
  }
  return status;
}
static constexpr zx_driver_ops_t stm_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = StmMcu::Create;
  return ops;
}();

}  // namespace stm

ZIRCON_DRIVER(vim3_mcu, stm::stm_driver_ops, "zircon", "0.1");
