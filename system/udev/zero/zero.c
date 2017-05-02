// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mx_status_t zero_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    memset(buf, 0, count);
    *actual = count;
    return NO_ERROR;
}

static mx_status_t zero_write(void* ctx, const void* buf, size_t count, mx_off_t off,
                              size_t* actual) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t zero_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = zero_read,
    .write = zero_write,
};

mx_status_t zero_bind(mx_driver_t* drv, mx_device_t* parent, void** cookie) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "zero",
        .driver = drv,
        .ops = &zero_device_proto,
    };

    mx_device_t* dev;
    return device_add2(parent, &args, &dev);
}

#if !DEVHOST_V2
mx_status_t zero_init(mx_driver_t* drv) {
    return zero_bind(drv, driver_get_root_device(), NULL);
}
#endif

static mx_driver_ops_t zero_driver_ops = {
    .version = DRIVER_OPS_VERSION,
#if DEVHOST_V2
    .bind = zero_bind,
#else
    .init = zero_init,
#endif
};

MAGENTA_DRIVER_BEGIN(zero, zero_driver_ops, "magenta", "0.1", 0)
MAGENTA_DRIVER_END(zero)
