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
  i2c_protocol_t i2c;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_I2C", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<StmMcu> device(new (&ac) StmMcu(parent, &i2c));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: StmMcu alloc failed", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }
  status = device->SetFanLevel(FL1);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("vim3-mcu")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __FILE__);
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

zx_status_t StmMcu::SetFanLevel(FanLevel level) {
  // TODO fxbug.dev/56400: rsubr clean this up to implement outward intf for rd/wr
  // for now just turning on the fan to prevent soc from overheating
  fbl::AutoLock lock(&i2c_lock_);
  uint8_t cmd[] = {STM_MCU_REG_CMD_FAN_STATUS_CTRL_REG, static_cast<uint8_t>(level)};
  auto status = i2c_.WriteSync(cmd, sizeof(cmd));
  if (status != ZX_OK) {
    zxlogf(ERROR, "StmMCu::could not set the fan level %d", status);
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
