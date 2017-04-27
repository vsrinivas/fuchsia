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

static ssize_t null_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    return 0;
}

static ssize_t null_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    return count;
}

static mx_protocol_device_t null_device_proto = {
    .read = null_read,
    .write = null_write,
};

// implement driver object:

mx_status_t null_init(mx_driver_t* driver) {
    mx_device_t* dev;
    if (device_create("null", NULL, &null_device_proto, driver, &dev) == NO_ERROR) {
        if (device_add(dev, driver_get_root_device()) < 0) {
            free(dev);
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_null;

static mx_driver_ops_t null_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = null_init,
};

MAGENTA_DRIVER_BEGIN(null, null_driver_ops, "magenta", "0.1", 0)
MAGENTA_DRIVER_END(null)
