// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <zircon/device/sysinfo.h>
#include <zircon/syscalls/resource.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>


#define ID_HJOBROOT 4
static mtx_t sysinfo_lock = MTX_INIT;
static zx_handle_t sysinfo_job_root;

static zx_handle_t get_sysinfo_job_root(void) {
    mtx_lock(&sysinfo_lock);
    if (sysinfo_job_root == ZX_HANDLE_INVALID) {
        sysinfo_job_root = zx_get_startup_handle(PA_HND(PA_USER0, ID_HJOBROOT));
    }
    mtx_unlock(&sysinfo_lock);

    zx_handle_t h;
    if ((sysinfo_job_root != ZX_HANDLE_INVALID) &&
        (zx_handle_duplicate(sysinfo_job_root, ZX_RIGHT_SAME_RIGHTS, &h) == ZX_OK)) {
        return h;
    }

    return ZX_HANDLE_INVALID;
}

static zx_status_t sysinfo_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                             void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_SYSINFO_GET_ROOT_JOB: {
        if ((cmdlen != 0) || (max < sizeof(zx_handle_t))) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t h = get_sysinfo_job_root();
        if (h == ZX_HANDLE_INVALID) {
            return ZX_ERR_NOT_SUPPORTED;
        } else {
            memcpy(reply, &h, sizeof(zx_handle_t));
            *out_actual = sizeof(zx_handle_t);
            return ZX_OK;
        }
    }
    case IOCTL_SYSINFO_GET_ROOT_RESOURCE: {
        if ((cmdlen != 0) || (max < sizeof(zx_handle_t))) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t h = get_root_resource();
        if (h == ZX_HANDLE_INVALID) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        zx_status_t status = zx_handle_duplicate(h, ZX_RIGHT_TRANSFER, &h);
        if (status < 0) {
            return status;
        }
        memcpy(reply, &h, sizeof(zx_handle_t));
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    case IOCTL_SYSINFO_GET_HYPERVISOR_RESOURCE: {
        if ((cmdlen != 0) || (max < sizeof(zx_handle_t))) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t h;
        zx_status_t status = zx_resource_create(get_root_resource(),
                                                ZX_RSRC_KIND_HYPERVISOR,
                                                0, 0, &h);
        if (status < 0) {
            return status;
        }
        memcpy(reply, &h, sizeof(zx_handle_t));
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_protocol_device_t sysinfo_ops = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = sysinfo_ioctl,
};

zx_status_t sysinfo_bind(void* ctx, zx_device_t* parent) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sysinfo",
        .ops = &sysinfo_ops,
    };

    zx_device_t* dev;
    return device_add(parent, &args, &dev);
}

static zx_driver_ops_t sysinfo_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sysinfo_bind,
};

ZIRCON_DRIVER_BEGIN(sysinfo, sysinfo_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(sysinfo)
