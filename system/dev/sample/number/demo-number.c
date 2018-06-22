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

#include "demo-number.h"
#include <zircon/sample/number/c/fidl.h>

/*
 *  This is the /dev/misc/demo-number device.
 *
 *  It implements a simple device that can be used from the command line, for
 *  example "cat /dev/misc/demo-number" to return the next number in the
 *  sequence.
 *
 *  It illustrates:
 *      - handling read and ioctl
 *      - maintaining per-device and per-session context
*/

// our per-device context structure
typedef struct {
    zx_device_t*        zxdev;
    atomic_int_fast64_t counter;
} number_device_t;

static zx_status_t number_ioctl(void* ctx, uint32_t op,
                                const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len,
                                size_t* out_actual) {
    number_device_t* device = ctx;

    switch (op) {
    case    IOCTL_DEV_NUMBER_RESET:
        if (in_len != sizeof(int)) {
            return ZX_ERR_INVALID_ARGS;
        }
        device->counter = *(int*) in_buf;
        return ZX_OK;

    }
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t number_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    if (off == 0) {
        number_device_t* device = ctx;
        int n = atomic_fetch_add(&device->counter, 1);
        char tmp[22];   // 2^64 is 20 digits + \n + \0
        *actual = snprintf(tmp, sizeof(tmp), "%d\n", n);
        if (*actual > count) *actual = count;
        memcpy(buf, tmp, *actual);
    } else {
        *actual = 0;
    }
    return ZX_OK;
}

static zx_status_t fidl_SetNumber(void* ctx, uint32_t value, fidl_txn_t* txn)
{
    number_device_t* device = ctx;
    int saved = device->counter;
    device->counter = value;
    return zircon_sample_number_NumberSetNumber_reply(txn, saved);
}

static zircon_sample_number_Number_ops_t number_fidl_ops = {
    .SetNumber = fidl_SetNumber,
};

static zx_status_t number_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    zx_status_t status = zircon_sample_number_Number_dispatch(ctx, txn, msg, &number_fidl_ops);
    return status;
}

static void number_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t number_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = number_ioctl,
    .read = number_read,
    .release = number_release,
    .message = number_message,
};

static zx_status_t number_bind(void* ctx, zx_device_t* parent) {
    // allocate & initialize per-device context block
    number_device_t* device = calloc(1, sizeof(*device));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "demo-number",
        .ops = &number_device_ops,
        .ctx = device,
    };

    zx_status_t rc = device_add(parent, &args, &device->zxdev);
    if (rc != ZX_OK) {
        free(device);
    }
    return rc;
}

static zx_driver_ops_t demo_number_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = number_bind,
};

ZIRCON_DRIVER_BEGIN(demo_number_driver, demo_number_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(demo_number_driver)

