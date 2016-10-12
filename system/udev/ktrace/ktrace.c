// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/ktrace.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <magenta/device/ktrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static ssize_t ktrace_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    return mx_ktrace_read(get_root_resource(), buf, off, count);
}

static mx_off_t ktrace_get_size(mx_device_t* dev) {
    return mx_ktrace_read(get_root_resource(), NULL, 0, 0);
}

static ssize_t ktrace_ioctl(mx_device_t* dev, uint32_t op,
                            const void* cmd, size_t cmdlen,
                            void* reply, size_t max) {
    switch (op) {
    case IOCTL_KTRACE_GET_HANDLE: {
        if (max < sizeof(mx_handle_t)) {
            return ERR_BUFFER_TOO_SMALL;
        }
        //TODO: ktrace-only handle once resources are further along
        mx_handle_t h = mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS);
        if (h < 0) {
            return h;
        }
        *((mx_handle_t*) reply) = h;
        return sizeof(mx_handle_t);
    }
    case IOCTL_KTRACE_ADD_PROBE: {
        char name[MX_MAX_NAME_LEN];
        if ((cmdlen >= MX_MAX_NAME_LEN) || (cmdlen < 1) || (max != sizeof(uint32_t))) {
            return ERR_INVALID_ARGS;
        }
        memcpy(name, cmd, cmdlen);
        name[cmdlen] = 0;
        mx_status_t status = mx_ktrace_control(get_root_resource(), KTRACE_ACTION_NEW_PROBE, 0, name);
        if (status < 0) {
            return status;
        }
        *((uint32_t*) reply) = status;
        return sizeof(uint32_t);
    }
    default:
        return ERR_INVALID_ARGS;
    }
}

static mx_protocol_device_t ktrace_device_proto = {
    .read = ktrace_read,
    .ioctl = ktrace_ioctl,
    .get_size = ktrace_get_size,
};

mx_status_t ktrace_init(mx_driver_t* driver) {
    mx_device_t* dev;
    if (device_create(&dev, driver, "ktrace", &ktrace_device_proto) == NO_ERROR) {
        mx_status_t status;
        if ((status = device_add(dev, driver_get_misc_device())) < 0) {
            free(dev);
            return status;
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_ktrace = {
    .ops = {
        .init = ktrace_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_ktrace, "ktrace", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_ktrace)
