// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/*
 *  This is the /dev/misc/demo-multi device.
 *
 *  It implements a simple device with multiple sub-devices.
 *  Each sub-device can be tested from the command line via cat,
 *  for example:
 *
 *  $ cat /dev/misc/demo-multi/13
 *  thirteen
 *  $ cat /dev/misc/demo-mutli/2
 *  two
 *
 *  That is, the device simply returns that ASCII representation of
 *  its device name via read().
 *
 *  This builds on the concepts introduced in /dev/misc/demo-number.
*/

// this contains our sub-devices
#define NDEVICES 16
typedef struct {
    zx_device_t*    zxdev;
    int             devno;              // device number (index)
} multidev_t;

// this contains our per-device instance
typedef struct {
    zx_device_t*    parent;
    multidev_t*     devices[NDEVICES];  // dynamically allocated
    multidev_t      base_device;
} multi_root_device_t;

static const char* devnames[NDEVICES] = {
    "zero", "one", "two", "three",
    "four", "five", "six", "seven",
    "eight", "nine", "ten", "eleven",
    "twelve", "thirteen", "fourteen", "fifteen",
};

static zx_status_t multi_base_read(void* ctx, void* buf, size_t count,
                                   zx_off_t off, size_t* actual) {
    const char* base_name = "base device\n";

    if (off == 0) {
        *actual = strlen(base_name);
        if (*actual > count) {
            *actual = count;
        }
        memcpy(buf, base_name, *actual);
    } else {
        *actual = 0;
    }
    return ZX_OK;
}

static zx_status_t multi_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    multidev_t* device = ctx;

    if (off == 0) {
        char tmp[16];
        *actual = snprintf(tmp, sizeof(tmp), "%s\n", devnames[device->devno]);
        if (*actual > count) {
            *actual = count;
        }
        memcpy(buf, tmp, *actual);
    } else {
        *actual = 0;
    }
    return ZX_OK;
}

static void multi_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t multi_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .read = multi_read,
    .release = multi_release,
};

static zx_protocol_device_t multi_base_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .read = multi_base_read,
    .release = multi_release,
};

zx_status_t multi_bind(void* ctx, zx_device_t* parent) {
    // allocate & initialize per-device context block
    multi_root_device_t* device = calloc(1, sizeof(*device));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }
    device->parent = parent;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .ops = &multi_base_device_ops,
        .name = "demo-multi",
        .ctx = &device->base_device,
    };

    zx_status_t rc = device_add(parent, &args, &device->base_device.zxdev);
    if (rc != ZX_OK) {
        return rc;
    }

    args.ops = &multi_device_ops;               // switch to sub-device ops
    for (int i = 0; i < NDEVICES; i++) {
        char name[ZX_DEVICE_NAME_MAX + 1];
        sprintf(name, "%d", i);
        args.name = name;                       // change name for each sub-device
        device->devices[i] = calloc(1, sizeof(*device->devices[i]));
        if (device->devices[i]) {
            args.ctx = device->devices[i];      // store device pointer in context
            device->devices[i]->devno = i;      // store number as part of context
            rc = device_add(device->base_device.zxdev, &args, &device->devices[i]->zxdev);
            if (rc != ZX_OK) {
                free(device->devices[i]);       // device "i" failed; free its memory
            }
        } else {
            rc = ZX_ERR_NO_MEMORY;              // set up error code
        }
        if (rc != ZX_OK) {
            for (int j = 0; j < i; j++) {
                device_remove(device->devices[j]->zxdev);
                free(device->devices[j]);
            }
            device_remove(device->base_device.zxdev);
            free(device);
            return rc;
        }
    }

    return rc;
}

static zx_driver_ops_t demo_multi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = multi_bind,
};

ZIRCON_DRIVER_BEGIN(demo_multi_driver, demo_multi_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(demo_multi_driver)

