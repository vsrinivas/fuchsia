// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/ktrace.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zircon/device/ktrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static zx_status_t ktrace_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    uint32_t length;
    zx_status_t status = zx_ktrace_read(get_root_resource(), buf, off, count, &length);
    if (status == ZX_OK) {
        *actual = length;
    }
    return status;
}

static zx_off_t ktrace_get_size(void* ctx) {
    uint32_t size;
    zx_status_t status = zx_ktrace_read(get_root_resource(), NULL, 0, 0, &size);
    return status != ZX_OK ? (zx_off_t)status : (zx_off_t)size;
}

static zx_status_t ktrace_ioctl(void* ctx, uint32_t op,
                            const void* cmd, size_t cmdlen,
                            void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_KTRACE_GET_HANDLE: {
        if (max < sizeof(zx_handle_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        //TODO: ktrace-only handle once resources are further along
        zx_handle_t h;
        zx_status_t status = zx_handle_duplicate(get_root_resource(), ZX_RIGHT_SAME_RIGHTS, &h);
        if (status < 0) {
            return status;
        }
        *((zx_handle_t*) reply) = h;
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    case IOCTL_KTRACE_ADD_PROBE: {
        char name[ZX_MAX_NAME_LEN];
        if ((cmdlen >= ZX_MAX_NAME_LEN) || (cmdlen < 1) || (max != sizeof(uint32_t))) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(name, cmd, cmdlen);
        name[cmdlen] = 0;
        zx_status_t status = zx_ktrace_control(get_root_resource(), KTRACE_ACTION_NEW_PROBE, 0, name);
        if (status < 0) {
            return status;
        }
        *((uint32_t*) reply) = status;
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }
    case IOCTL_KTRACE_START: {
        if (cmdlen != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t group_mask = *(uint32_t *)cmd;
        return zx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, group_mask, NULL);
    }
    case IOCTL_KTRACE_STOP: {
        zx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0, NULL);
        zx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0, NULL);
        return ZX_OK;
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_protocol_device_t ktrace_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = ktrace_read,
    .ioctl = ktrace_ioctl,
    .get_size = ktrace_get_size,
};

static zx_status_t ktrace_bind(void* ctx, zx_device_t* parent) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ktrace",
        .ops = &ktrace_device_proto,
    };

    zx_device_t* dev;
    return device_add(parent, &args, &dev);
}

static zx_driver_ops_t ktrace_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ktrace_bind,
};

ZIRCON_DRIVER_BEGIN(ktrace, ktrace_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ktrace)
