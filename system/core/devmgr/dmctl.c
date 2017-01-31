// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/devmgr.h>

#include <magenta/device/dmctl.h>
#include <magenta/types.h>

#include <mxio/io.h>
#include <mxio/loader-service.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devhost.h"

static ssize_t dmctl_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    char cmd[1024];
    if (count < sizeof(cmd)) {
        memcpy(cmd, buf, count);
        cmd[count] = 0;
    } else {
        return ERR_INVALID_ARGS;
    }
    return devmgr_control(cmd);
}

static mxio_multiloader_t* multiloader = NULL;

static ssize_t dmctl_ioctl(mx_device_t* dev, uint32_t op,
                           const void* in_buf, size_t in_len,
                           void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL:
        if (in_len != 0 || out_buf == NULL || out_len != sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        if (multiloader == NULL) {
            // The allocation in dmctl_init() failed.
            return ERR_NO_MEMORY;
        }
        // Create a new channel on the multiloader.
        mx_handle_t out_channel = mxio_multiloader_new_service(multiloader);
        if (out_channel < 0) {
            return out_channel;
        }
        memcpy(out_buf, &out_channel, sizeof(mx_handle_t));
        return sizeof(mx_handle_t);
    default:
        return ERR_INVALID_ARGS;
    }
}

static mx_protocol_device_t dmctl_device_proto = {
    .write = dmctl_write,
    .ioctl = dmctl_ioctl,
};

mx_handle_t _dmctl_handle = MX_HANDLE_INVALID;

mx_status_t dmctl_init(mx_driver_t* driver) {
    // Don't try to ioctl to ourselves when this process loads libraries.
    // Call this before the device has been created; mxio_loader_service()
    // uses the device's presence as an invitation to use it.
    mxio_force_local_loader_service();

    mx_device_t* dev;
    mx_status_t s = device_create(&dev, driver, "dmctl", &dmctl_device_proto);
    if (s != NO_ERROR) {
        return s;
    }
    s = device_add(dev, driver_get_misc_device());
    if (s != NO_ERROR) {
        free(dev);
        return s;
    }
    _dmctl_handle = dev->rpc;

    // Loader service init.
    s = mxio_multiloader_create("dmctl-multiloader", &multiloader);
    if (s != NO_ERROR) {
        // If this fails, IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL will fail
        // and processes will fall back to using a local loader.
        // TODO: Make this fatal?
        printf("dmctl: cannot create multiloader context: %d\n", s);
    }

    return NO_ERROR;
}

mx_driver_t _driver_dmctl = {
    .name = "dmctl",
    .ops = {
        .init = dmctl_init,
    },
};
