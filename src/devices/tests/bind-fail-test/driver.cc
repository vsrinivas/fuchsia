// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>

#include "src/devices/tests/bind-fail-test/bind-fail-test-bind.h"

zx_status_t not_supported_bind(void* ctx, zx_device_t* parent) { return ZX_ERR_NOT_SUPPORTED; }

static zx_driver_ops_t bind_fail_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = not_supported_bind,
};

ZIRCON_DRIVER(bind_fail, bind_fail_driver_ops, "zircon", "0.1");
