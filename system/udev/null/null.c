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

// null is the /dev/null device.

// implement character protocol:

static ssize_t null_read(mx_device_t* dev, void* buf, size_t count) {
    return NO_ERROR;
}

static ssize_t null_write(mx_device_t* dev, const void* buf, size_t count) {
    return count;
}

static mx_protocol_char_t null_char_proto = {
    .read = null_read,
    .write = null_write,
};

// implement device protocol:

static mx_status_t null_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t null_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t null_release(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_protocol_device_t null_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = null_open,
    .close = null_close,
    .release = null_release,
};

// implement driver object:

mx_status_t null_init(mx_driver_t* driver) {
    mx_device_t* dev;
    if (device_create(&dev, driver, "null", &null_device_proto) == NO_ERROR) {
        dev->protocol_id = MX_PROTOCOL_CHAR;
        dev->protocol_ops = &null_char_proto;
        if (device_add(dev, NULL) < 0) {
            free(dev);
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_null BUILTIN_DRIVER = {
    .name = "null",
    .ops = {
        .init = null_init,
    },
};
