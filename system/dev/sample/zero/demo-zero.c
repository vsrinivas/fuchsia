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

// This is the /dev/misc/demo-zero device.

static zx_status_t zero_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    memset(buf, 0, count);
    *actual = count;
    return ZX_OK;
}

static zx_protocol_device_t zero_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .read = zero_read,
};

zx_status_t zero_bind(void* ctx, zx_device_t* parent) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "demo-zero",
        .ops = &zero_device_ops,
    };

    return device_add(parent, &args, NULL);
}

static zx_driver_ops_t demo_zero_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = zero_bind,
};

ZIRCON_DRIVER_BEGIN(demo_zero_driver, demo_zero_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(demo_zero_driver)
