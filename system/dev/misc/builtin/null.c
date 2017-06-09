// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// null is the /dev/null device.

static mx_status_t null_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    *actual = 0;
    return MX_OK;
}

static mx_status_t null_write(void* ctx, const void* buf, size_t count, mx_off_t off, size_t* actual) {
    *actual = count;
    return MX_OK;
}

static mx_protocol_device_t null_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = null_read,
    .write = null_write,
};

mx_status_t null_bind(void* ctx, mx_device_t* parent, void** cookie) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "null",
        .ops = &null_device_proto,
    };

    mx_device_t* dev;
    return device_add(parent, &args, &dev);
}
