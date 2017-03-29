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

static ssize_t zero_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    memset(buf, 0, count);
    return count;
}

static ssize_t zero_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t zero_device_proto = {
    .read = zero_read,
    .write = zero_write,
};

mx_status_t zero_init(mx_driver_t* driver) {
    mx_device_t* dev;
    if (device_create(&dev, driver, "zero", &zero_device_proto) == NO_ERROR) {
        if (device_add(dev, driver_get_root_device()) < 0) {
            free(dev);
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_zero = {
    .ops = {
        .init = zero_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_zero, "zero", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_zero)
