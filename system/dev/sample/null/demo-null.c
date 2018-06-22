// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This is the /dev/misc/demo-null device.

static zx_status_t null_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    *actual = 0;
    return ZX_OK;
}

static zx_status_t null_write(void* ctx, const void* buf, size_t count,
                              zx_off_t off, size_t* actual) {
    *actual = count;
    return ZX_OK;
}

static zx_protocol_device_t null_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .read = null_read,
    .write = null_write,
};

zx_status_t null_bind(void* ctx, zx_device_t* parent) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "demo-null",
        .ops = &null_device_ops,
    };

    return device_add(parent, &args, NULL);
}

static zx_driver_ops_t demo_null_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = null_bind,
};

ZIRCON_DRIVER_BEGIN(demo_null_driver, demo_null_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(demo_null_driver)

