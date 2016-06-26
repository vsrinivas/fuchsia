// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/char.h>

#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devmgr.h"

static ssize_t dmctl_read(mx_device_t* dev, void* buf, size_t count, size_t off) {
    return NO_ERROR;
}

static ssize_t dmctl_write(mx_device_t* dev, const void* buf, size_t count, size_t off) {
    char cmd[128];
    if (count < sizeof(cmd)) {
        memcpy(cmd, buf, count);
        cmd[count] = 0;
    } else {
        return ERR_INVALID_ARGS;
    }
    return devmgr_control(cmd);
}

static mx_protocol_char_t dmctl_char_proto = {
    .read = dmctl_read,
    .write = dmctl_write,
};

static mx_status_t dmctl_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t dmctl_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t dmctl_release(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_protocol_device_t dmctl_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = dmctl_open,
    .close = dmctl_close,
    .release = dmctl_release,
};

mx_status_t dmctl_init(mx_driver_t* driver) {
    mx_device_t* dev;
    if (device_create(&dev, driver, "dmctl", &dmctl_device_proto) == NO_ERROR) {
        dev->protocol_id = MX_PROTOCOL_CHAR;
        dev->protocol_ops = &dmctl_char_proto;
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
