// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "device-internal.h"
#include "devcoordinator.h"

#include <zircon/device/dmctl.h>
#include <launchpad/loader-service.h>

static zx_device_t* dmctl_dev;

static loader_service_t* loader_svc;

static zx_status_t dmctl_cmd(uint32_t op, const char* cmd, size_t cmdlen, zx_handle_t h) {
    dc_msg_t msg;
    uint32_t msglen;
    if (dc_msg_pack(&msg, &msglen, cmd, cmdlen, NULL, NULL) < 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    msg.op = op;
    dc_status_t rsp;
    return dc_msg_rpc(dmctl_dev->rpc, &msg, msglen,
                      &h, (h != ZX_HANDLE_INVALID) ? 1 : 0,
                      &rsp, sizeof(rsp));
}

static zx_status_t dmctl_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                               size_t* actual) {
    zx_status_t status = dmctl_cmd(DC_OP_DM_COMMAND, buf, count, ZX_HANDLE_INVALID);
    if (status >= 0) {
        *actual = count;
        status = ZX_OK;
    }
    return status;
}

static zx_status_t dmctl_ioctl(void* ctx, uint32_t op,
                               const void* in_buf, size_t in_len,
                               void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL:
        if (in_len != 0 || out_buf == NULL || out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (loader_svc == NULL) {
            // The allocation in dmctl_init() failed.
            return ZX_ERR_NO_MEMORY;
        }
        // Create a new channel on the multiloader.
        zx_handle_t out_channel;
        zx_status_t status = loader_service_connect(loader_svc, &out_channel);
        if (status < 0) {
            return status;
        }
        memcpy(out_buf, &out_channel, sizeof(zx_handle_t));
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    case IOCTL_DMCTL_COMMAND:
        if (in_len != sizeof(dmctl_cmd_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        dmctl_cmd_t cmd;
        memcpy(&cmd, in_buf, sizeof(cmd));
        cmd.name[sizeof(cmd.name) - 1] = 0;
        *out_actual = 0;
        status = dmctl_cmd(DC_OP_DM_COMMAND, cmd.name, strlen(cmd.name), cmd.h);
        // NOT_SUPPORTED tells the dispatcher to close the handle for
        // ioctls that accept a handle argument, so we have to avoid
        // returning that in this case where the handle has been passed
        // to another process (and effectively closed)
        if (status == ZX_ERR_NOT_SUPPORTED) {
            status = ZX_ERR_INTERNAL;
        }
        return status;
    case IOCTL_DMCTL_OPEN_VIRTCON:
        if (in_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return dmctl_cmd(DC_OP_DM_OPEN_VIRTCON, NULL, 0, *((zx_handle_t*) in_buf));
    case IOCTL_DMCTL_WATCH_DEVMGR:
        if (in_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return dmctl_cmd(DC_OP_DM_WATCH, NULL, 0, *((zx_handle_t*) in_buf));
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_protocol_device_t dmctl_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .write = dmctl_write,
    .ioctl = dmctl_ioctl,
};

zx_status_t dmctl_bind(void* ctx, zx_device_t* parent, void** cookie) {

    // Don't try to ioctl to ourselves when this process loads libraries.
    // Call this before the device has been created; fdio_loader_service()
    // uses the device's presence as an invitation to use it.
    loader_service_force_local();

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dmctl",
        .ops = &dmctl_device_ops,
    };

    zx_status_t status;
    if ((status = device_add(parent, &args, &dmctl_dev)) < 0) {
        return status;
    }

    // Loader service init.
    if ((status = loader_service_create_fs("dmctl-multiloader", &loader_svc)) < 0) {
        // If this fails, IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL will fail
        // and processes will fall back to using a local loader.
        // TODO: Make this fatal?
        printf("dmctl: cannot create multiloader context: %d\n", status);
    }

    return ZX_OK;
}

static zx_driver_ops_t dmctl_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dmctl_bind,
};

ZIRCON_DRIVER_BEGIN(dmctl, dmctl_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(dmctl)
