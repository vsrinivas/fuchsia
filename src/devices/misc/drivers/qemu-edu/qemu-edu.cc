// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qemu-edu.h"

#include <lib/ddk/driver.h>

#include "src/devices/misc/drivers/qemu-edu/qemu_edu_bind.h"

namespace qemu_edu {

zx_status_t QemuEduDevice::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<QemuEduDevice>(parent);

  zx_status_t status = dev->DdkAdd(ddk::DeviceAddArgs("qemu-edu")
                                       .set_flags(DEVICE_ADD_NON_BINDABLE)
                                       .set_inspect_vmo(dev->inspector_.DuplicateVmo()));

  if (status != ZX_OK) {
    zxlogf(ERROR, "ddk add failed, st = %d", status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = QemuEduDevice::Create;
  return ops;
}();

}  // namespace qemu_edu

// clang-format off
ZIRCON_DRIVER(qemu-edu, qemu_edu::driver_ops, "zircon", "0.1");
