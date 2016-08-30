// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/devmgr.h>

#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mxio/io.h>

#include "devmgr.h"

static ssize_t dmctl_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    char cmd[128];
    if (count < sizeof(cmd)) {
        memcpy(cmd, buf, count);
        cmd[count] = 0;
    } else {
        return ERR_INVALID_ARGS;
    }
    return devmgr_control(cmd);
}

mx_status_t vfs_install_remote(mx_handle_t h);

static ssize_t dmctl_ioctl(mx_device_t* dev, uint32_t op,
                           const void* in_buf, size_t in_len,
                           void* out_buf, size_t out_len) {
    if (op != IOCTL_DEVMGR_MOUNT_FS) {
        return ERR_NOT_SUPPORTED;
    }
    if (out_len < sizeof(mx_handle_t)) {
        return ERR_INVALID_ARGS;
    }
    if ((in_len < 1) || (((char*)in_buf)[in_len - 1] != 0)) {
        return ERR_INVALID_ARGS;
    }
    if (strcmp(in_buf, "fs:/data")) {
        return ERR_NOT_FOUND;
    }

    mx_handle_t h[2];
    mx_status_t r;
    if ((r = mx_msgpipe_create(h, 0)) < 0) {
        return r;
    }
    if ((r = vfs_install_remote(h[1])) < 0) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        return r;
    }
    memcpy(out_buf, h, sizeof(mx_handle_t));
    return sizeof(mx_handle_t);
}

static mx_protocol_device_t dmctl_device_proto = {
    .write = dmctl_write,
    .ioctl = dmctl_ioctl,
};

mx_status_t dmctl_init(mx_driver_t* driver) {
    mx_device_t* dev;
    if (device_create(&dev, driver, "dmctl", &dmctl_device_proto) == NO_ERROR) {
        if (device_add(dev, NULL) < 0) {
            free(dev);
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_dmctl BUILTIN_DRIVER = {
    .name = "dmctl",
    .ops = {
        .init = dmctl_init,
    },
};
