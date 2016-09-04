// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

extern mx_handle_t root_resource_handle;

static ssize_t ktrace_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    return mx_ktrace_read(root_resource_handle, buf, off, count);
}

static mx_off_t ktrace_get_size(mx_device_t* dev) {
    return mx_ktrace_read(root_resource_handle, NULL, 0, 0);
}

static mx_protocol_device_t ktrace_device_proto = {
    .read = ktrace_read,
    .get_size = ktrace_get_size,
};

mx_status_t ktrace_init(mx_driver_t* driver) {
    mx_device_t* dev;
    printf("console_init()\n");
    if (device_create(&dev, driver, "ktrace", &ktrace_device_proto) == NO_ERROR) {
        mx_status_t status;
        if ((status = device_add(dev, NULL)) < 0) {
            free(dev);
            return status;
        }
    }
    return NO_ERROR;
}

mx_driver_t _ktrace_console BUILTIN_DRIVER = {
    .name = "ktrace",
    .ops = {
        .init = ktrace_init,
    },
};
