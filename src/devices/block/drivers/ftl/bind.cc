// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <memory>

#include <fbl/alloc_checker.h>

#include "block_device.h"
#include "src/devices/block/drivers/ftl/ftl_bind.h"

namespace {

zx_status_t FtlDriverBind(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "FTL: Binding. Version 1.2.04 (update to NDM v2)");
  fbl::AllocChecker checker;
  std::unique_ptr<ftl::BlockDevice> device(new (&checker) ftl::BlockDevice(parent));
  if (!checker.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED ftl::BlockDevice* dummy = device.release();
  }
  return status;
}

}  // namespace

static constexpr zx_driver_ops_t ftl_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = FtlDriverBind;
  return ops;
}();

ZIRCON_DRIVER(ftl, ftl_driver_ops, "zircon", "0.1");
