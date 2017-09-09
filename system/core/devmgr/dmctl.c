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

#include <magenta/device/dmctl.h>
#include <launchpad/loader-service.h>

static mx_device_t* dmctl_dev;

static loader_service_t* loader_svc;

static mx_status_t dmctl_cmd(uint32_t op, const char* cmd, size_t cmdlen, mx_handle_t h) {
    dc_msg_t msg;
    uint32_t msglen;
    if (dc_msg_pack(&msg, &msglen, cmd, cmdlen, NULL, NULL) < 0) {
        return MX_ERR_INVALID_ARGS;
    }
    msg.op = op;
    dc_status_t rsp;
    return dc_msg_rpc(dmctl_dev->rpc, &msg, msglen,
                      &h, (h != MX_HANDLE_INVALID) ? 1 : 0,
                      &rsp, sizeof(rsp));
}

static mx_status_t dmctl_write(void* ctx, const void* buf, size_t count, mx_off_t off,
                               size_t* actual) {
    mx_status_t status = dmctl_cmd(DC_OP_DM_COMMAND, buf, count, MX_HANDLE_INVALID);
    if (status >= 0) {
        *actual = count;
        status = MX_OK;
    }
    return status;
}

static mx_status_t dmctl_ioctl(void* ctx, uint32_t op,
                               const void* in_buf, size_t in_len,
                               void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL:
        if (in_len != 0 || out_buf == NULL || out_len != sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        if (loader_svc == NULL) {
            // The allocation in dmctl_init() failed.
            return MX_ERR_NO_MEMORY;
        }
        // Create a new channel on the multiloader.
        mx_handle_t out_channel;
        mx_status_t status = loader_service_connect(loader_svc, &out_channel);
        if (status < 0) {
            return status;
        }
        memcpy(out_buf, &out_channel, sizeof(mx_handle_t));
        *out_actual = sizeof(mx_handle_t);
        return MX_OK;
    case IOCTL_DMCTL_COMMAND:
        if (in_len != sizeof(dmctl_cmd_t)) {
            return MX_ERR_INVALID_ARGS;
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
        if (status == MX_ERR_NOT_SUPPORTED) {
            status = MX_ERR_INTERNAL;
        }
        return status;
    case IOCTL_DMCTL_OPEN_VIRTCON:
        if (in_len != sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        return dmctl_cmd(DC_OP_DM_OPEN_VIRTCON, NULL, 0, *((mx_handle_t*) in_buf));
    case IOCTL_DMCTL_WATCH_DEVMGR:
        if (in_len != sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        return dmctl_cmd(DC_OP_DM_WATCH, NULL, 0, *((mx_handle_t*) in_buf));
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

static mx_protocol_device_t dmctl_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .write = dmctl_write,
    .ioctl = dmctl_ioctl,
};

mx_status_t dmctl_bind(void* ctx, mx_device_t* parent, void** cookie) {

    // Don't try to ioctl to ourselves when this process loads libraries.
    // Call this before the device has been created; mxio_loader_service()
    // uses the device's presence as an invitation to use it.
    loader_service_force_local();

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dmctl",
        .ops = &dmctl_device_ops,
    };

    mx_status_t status;
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

    return MX_OK;
}

static mx_driver_ops_t dmctl_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dmctl_bind,
};

MAGENTA_DRIVER_BEGIN(dmctl, dmctl_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT),
MAGENTA_DRIVER_END(dmctl)
