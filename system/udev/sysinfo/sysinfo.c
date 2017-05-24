// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <magenta/device/sysinfo.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#define ID_HJOBROOT 4
static mtx_t sysinfo_lock = MTX_INIT;
static mx_handle_t sysinfo_job_root;

static mx_handle_t get_sysinfo_job_root(void) {
    mtx_lock(&sysinfo_lock);
    if (sysinfo_job_root == MX_HANDLE_INVALID) {
        sysinfo_job_root = mx_get_startup_handle(PA_HND(PA_USER0, ID_HJOBROOT));
    }
    mtx_unlock(&sysinfo_lock);

    mx_handle_t h;
    if ((sysinfo_job_root != MX_HANDLE_INVALID) &&
        (mx_handle_duplicate(sysinfo_job_root, MX_RIGHT_SAME_RIGHTS, &h) == NO_ERROR)) {
        return h;
    }

    return MX_HANDLE_INVALID;
}

static mx_status_t sysinfo_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                             void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_SYSINFO_GET_ROOT_JOB: {
        if ((cmdlen != 0) || (max < sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h = get_sysinfo_job_root();
        if (h == MX_HANDLE_INVALID) {
            return ERR_NOT_SUPPORTED;
        } else {
            memcpy(reply, &h, sizeof(mx_handle_t));
            *out_actual = sizeof(mx_handle_t);
            return NO_ERROR;
        }
    }
    case IOCTL_SYSINFO_GET_ROOT_RESOURCE: {
        if ((cmdlen != 0) || (max < sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h = get_root_resource();
        if (h == MX_HANDLE_INVALID) {
            return ERR_NOT_SUPPORTED;
        }
        mx_status_t status = mx_handle_duplicate(h, MX_RIGHT_ENUMERATE | MX_RIGHT_TRANSFER, &h);
        if (status < 0) {
            return status;
        }
        memcpy(reply, &h, sizeof(mx_handle_t));
        *out_actual = sizeof(mx_handle_t);
        return NO_ERROR;
    }
    default:
        return ERR_INVALID_ARGS;
    }
}

static mx_protocol_device_t sysinfo_ops = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = sysinfo_ioctl,
};

mx_status_t sysinfo_bind(void* ctx, mx_device_t* parent, void** cookie) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sysinfo",
        .ops = &sysinfo_ops,
    };

    mx_device_t* dev;
    return device_add(parent, &args, &dev);
}

static mx_driver_ops_t sysinfo_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sysinfo_bind,
};

MAGENTA_DRIVER_BEGIN(sysinfo, sysinfo_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT),
MAGENTA_DRIVER_END(sysinfo)
