// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rpi4.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

namespace rpi4 {

zx_status_t Rpi4::Create(void* ctx, zx_device_t* parent) {

  pbus_protocol_t pbus;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  iommu_protocol_t iommu;
  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Rpi4>(&ac, parent, &pbus, &iommu);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("rpi4");
  if (status != ZX_OK) {
    return status;
  }

  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }

  return status;
}

int Rpi4::Thread() {
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
  if ((status = NnaInit()) != ZX_OK) {
    zxlogf(ERROR, "NnaInit() failed: %d", status);
    init_txn_->Reply(ZX_ERR_INTERNAL);
    return status;
  }

  init_txn_->Reply(status);
  return ZX_OK;
}

void Rpi4::DdkInit(ddk::InitTxn txn) {

  init_txn_ = std::move(txn);
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Rpi4*>(arg)->Thread(); }, this,
      "rpi4-start-thread");
  if (rc != thrd_success) {
    init_txn_->Reply(ZX_ERR_INTERNAL);
  }
}

static constexpr zx_driver_ops_t rpi4_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Rpi4::Create;
  return ops;
}();

}  // namespace rpi4
// clang-format off
ZIRCON_DRIVER_BEGIN(rpi4, rpi4::rpi4_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_BCM2711),
ZIRCON_DRIVER_END(rpi4)
    // clang-format on
