// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/vim3/vim3.h"

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

#include "src/devices/board/drivers/vim3/vim3-bind.h"

namespace vim3 {

zx_status_t Vim3::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  iommu_protocol_t iommu;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Vim3>(&ac, parent, &pbus, &iommu);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("vim3");
  if (status != ZX_OK) {
    return status;
  }

  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }

  return status;
}

int Vim3::Thread() {
  // Load protocol implementation drivers first.
  zx_status_t status;

  if ((status = SysmemInit()) != ZX_OK) {
    zxlogf(ERROR, "SysmemInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = GpioInit()) != ZX_OK) {
    zxlogf(ERROR, "GpioInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = RegistersInit()) != ZX_OK) {
    zxlogf(ERROR, "RegistersInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = ClkInit()) != ZX_OK) {
    zxlogf(ERROR, "ClkInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = I2cInit()) != ZX_OK) {
    zxlogf(ERROR, "I2cInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = EthInit()) != ZX_OK) {
    zxlogf(ERROR, "EthInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = EmmcInit()) != ZX_OK) {
    zxlogf(ERROR, "EmmcInit() failed: %d\n", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = SdInit()) != ZX_OK) {
    zxlogf(ERROR, "SdInit() failed: %d\n", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = SdioInit()) != ZX_OK) {
    zxlogf(ERROR, "SdioInit() failed: %d\n", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = MaliInit()) != ZX_OK) {
    zxlogf(ERROR, "MaliInit() failed: %d\n", status);
  }
  if ((status = NnaInit()) != ZX_OK) {
    zxlogf(ERROR, "NnaInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  /* TODO(fxb/70442): re-enable USB when bug is fixed
  if ((status = UsbInit()) != ZX_OK) {
    zxlogf(ERROR, "UsbInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  */
  if ((status = CanvasInit()) != ZX_OK) {
    zxlogf(ERROR, "CanvasInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = DisplayInit()) != ZX_OK) {
    zxlogf(ERROR, "DisplayInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  if ((status = PwmInit()) != ZX_OK) {
    zxlogf(ERROR, "PwmInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }
  init_txn_->Reply(status);
  return ZX_OK;
}

void Vim3::DdkInit(ddk::InitTxn txn) {
  init_txn_ = std::move(txn);
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Vim3*>(arg)->Thread(); }, this,
      "vim3-start-thread");
  if (rc != thrd_success) {
    init_txn_->Reply(ZX_ERR_INTERNAL);
  }
}

static constexpr zx_driver_ops_t vim3_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Vim3::Create;
  return ops;
}();

}  // namespace vim3

ZIRCON_DRIVER(vim3, vim3::vim3_driver_ops, "zircon", "0.1");
