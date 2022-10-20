// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/buckeye/buckeye.h"

#include <assert.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/devices/board/drivers/buckeye/buckeye-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace buckeye {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Buckeye::Create(void* ctx, zx_device_t* parent) {
  iommu_protocol_t iommu;

  auto endpoints = fdf::CreateEndpoints<fuchsia_hardware_platform_bus::PlatformBus>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }
  zx_status_t status = device_connect_runtime_protocol(
      parent, fpbus::Service::PlatformBus::ServiceName, fpbus::Service::PlatformBus::Name,
      endpoints->server.TakeHandle().release());
  if (status != ZX_OK) {
    return status;
  }

  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Buckeye>(&ac, parent, std::move(endpoints->client), &iommu);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("buckeye");
  if (status != ZX_OK) {
    return status;
  }

  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* unused = board.release();
  }

  return status;
}

int Buckeye::Thread() {
  // Load protocol implementation drivers first.
  zx_status_t status;

  zxlogf(INFO, "Initializing BUCKEYE board!!!");

  if ((status = GpioInit()) != ZX_OK) {
    zxlogf(ERROR, "GpioInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = PwmInit()) != ZX_OK) {
    zxlogf(ERROR, "PwmInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = ClkInit()) != ZX_OK) {
    zxlogf(ERROR, "ClkInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = I2cInit()) != ZX_OK) {
    zxlogf(ERROR, "I2cInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = RegistersInit()) != ZX_OK) {
    zxlogf(ERROR, "RegistersInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = EmmcInit()) != ZX_OK) {
    zxlogf(ERROR, "EmmcInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = SpiInit()) != ZX_OK) {
    zxlogf(ERROR, "SpiInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = SdioInit()) != ZX_OK) {
    zxlogf(ERROR, "SdioInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = RtcInit()) != ZX_OK) {
    zxlogf(ERROR, "RtcInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = AudioInit()) != ZX_OK) {
    zxlogf(ERROR, "AudioInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = UsbInit()) != ZX_OK) {
    zxlogf(ERROR, "UsbInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = ThermalInit()) != ZX_OK) {
    zxlogf(ERROR, "ThermalInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = SysmemInit()) != ZX_OK) {
    zxlogf(ERROR, "SysmemInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = TeeInit()) != ZX_OK) {
    zxlogf(ERROR, "TeeInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = PowerInit()) != ZX_OK) {
    zxlogf(ERROR, "PowerInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = CpuInit()) != ZX_OK) {
    zxlogf(ERROR, "CpuInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = DmcInit()) != ZX_OK) {
    zxlogf(ERROR, "DmcInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  if ((status = ButtonsInit()) != ZX_OK) {
    zxlogf(ERROR, "ButtonsInit() failed: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
    return status;
  }
  init_txn_->Reply(status);
  return ZX_OK;
}

void Buckeye::DdkInit(ddk::InitTxn txn) {
  init_txn_ = std::move(txn);
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Buckeye*>(arg)->Thread(); }, this,
      "buckeye-start-thread");
  if (rc != thrd_success) {
    init_txn_->Reply(ZX_ERR_INTERNAL);
  }
}

static constexpr zx_driver_ops_t buckeye_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Buckeye::Create;
  return ops;
}();

}  // namespace buckeye

ZIRCON_DRIVER(buckeye, buckeye::buckeye_driver_ops, "zircon", "0.1");
