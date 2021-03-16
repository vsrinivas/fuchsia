// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qemu-bus.h"

#include <assert.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/threads.h>

#include <fbl/alloc_checker.h>

#include "src/devices/board/drivers/qemu-arm64/qemu_bus_bind.h"

namespace board_qemu_arm64 {

static bool use_fake_display() {
  const char* ufd = getenv("driver.qemu_bus.use_fake_display");
  return (ufd != nullptr && (!strcmp(ufd, "1") || !strcmp(ufd, "true") || !strcmp(ufd, "on")));
}

int QemuArm64::Thread() {
  zx_status_t status;
  zxlogf(INFO, "qemu-bus thread running ");

  if (use_fake_display()) {
    status = DisplayInit();
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DisplayInit() failed %d", __func__, status);
      return thrd_error;
    }
    zxlogf(INFO, "qemu.use_fake_display=1, disabling goldfish-display");
    setenv("driver.goldfish-display.disable", "true", 1);
  }

  status = PciInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PciInit() failed %d", __func__, status);
    return thrd_error;
  }

  status = SysmemInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SysmemInit() failed %d", __func__, status);
    return thrd_error;
  }

  status = PciAdd();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PciAdd() failed %d", __func__, status);
    return thrd_error;
  }

  status = RtcInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: RtcInit() failed %d", __func__, status);
    return thrd_error;
  }

  return 0;
}

zx_status_t QemuArm64::Start() {
  auto cb = [](void* arg) -> int { return reinterpret_cast<QemuArm64*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "qemu-arm64");
  return thrd_status_to_zx_status(rc);
}

zx_status_t QemuArm64::Create(void* ctx, zx_device_t* parent) {
  ddk::PBusProtocolClient pbus(parent);
  if (!pbus.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PBUS", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<QemuArm64>(&ac, parent, pbus);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status;
  if ((status = board->DdkAdd("qemu-bus", DEVICE_ADD_NON_BINDABLE)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %d", __func__, status);
    return status;
  }

  if ((status = board->Start()) != ZX_OK) {
    return status;
  }

  __UNUSED auto* dummy = board.release();
  return ZX_OK;
}

}  // namespace board_qemu_arm64

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = board_qemu_arm64::QemuArm64::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(qemu_bus, driver_ops, "zircon", "0.1");
//clang-format on
