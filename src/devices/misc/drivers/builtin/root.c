// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>

#include "src/devices/misc/drivers/builtin/root_drivers_bind.h"

zx_status_t null_bind(void* ctx, zx_device_t* parent);
zx_status_t zero_bind(void* ctx, zx_device_t* parent);

zx_status_t root_bind(void* ctx, zx_device_t* parent) {
  null_bind(ctx, parent);
  zero_bind(ctx, parent);
  return ZX_OK;
}

static zx_driver_ops_t root_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = root_bind,
};

ZIRCON_DRIVER(root_drivers, root_driver_ops, "zircon", "0.1");
