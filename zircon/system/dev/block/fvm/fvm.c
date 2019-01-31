// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <limits.h>
#include <zircon/types.h>

#include "fvm-private.h"

static zx_status_t fvm_bind_c(void* ctx, zx_device_t* dev) {
    return fvm_bind(dev);
}

static zx_driver_ops_t fvm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fvm_bind_c,
};

ZIRCON_DRIVER_BEGIN(fvm, fvm_driver_ops, "zircon", "0.1", 2)
BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
    ZIRCON_DRIVER_END(fvm)
