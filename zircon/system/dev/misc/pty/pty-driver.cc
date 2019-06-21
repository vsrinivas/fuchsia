// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <zircon/status.h>

#include "pty-core.h"
#include "pty-fifo.h"

typedef struct pty_server_dev {
    pty_server_t srv;

    mtx_t lock;
    pty_fifo_t fifo;
} pty_server_dev_t;

static zx_device_t* pty_root;

static zx_status_t psd_recv(pty_server_t* ps, const void* data, size_t len, size_t* actual) {
    if (len == 0) {
        *actual = 0;
        return ZX_OK;
    }

    pty_server_dev_t* psd = static_cast<pty_server_dev_t*>(containerof(ps, pty_server_dev_t, srv));
    zxlogf(TRACE, "PTY Server Device %p recv\n", ps);

    bool was_empty = pty_fifo_is_empty(&psd->fifo);
    *actual = pty_fifo_write(&psd->fifo, data, len, false);
    if (was_empty && *actual) {
        device_state_set(ps->zxdev, DEV_STATE_READABLE);
    }

    if (*actual == 0) {
        return ZX_ERR_SHOULD_WAIT;
    } else {
        return ZX_OK;
    }
}

static zx_status_t psd_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    auto psd = static_cast<pty_server_dev_t*>(ctx);
    zxlogf(TRACE, "PTY Server Device %p read\n", psd);

    bool eof = false;

    mtx_lock(&psd->srv.lock);
    bool was_full = pty_fifo_is_full(&psd->fifo);
    size_t length = pty_fifo_read(&psd->fifo, buf, count);
    if (pty_fifo_is_empty(&psd->fifo)) {
        if (list_is_empty(&psd->srv.clients)) {
            eof = true;
        } else {
            device_state_clr(psd->srv.zxdev, DEV_STATE_READABLE);
        }
    }
    if (was_full && length) {
        pty_server_resume_locked(&psd->srv);
    }
    mtx_unlock(&psd->srv.lock);

    if (length > 0) {
        *actual = length;
        return ZX_OK;
    } else if (eof) {
        *actual = 0;
        return ZX_OK;
    } else {
        return ZX_ERR_SHOULD_WAIT;
    }
}

static zx_status_t psd_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                             size_t* actual) {
    auto psd = static_cast<pty_server_dev_t*>(ctx);
    zxlogf(TRACE, "PTY Server Device %p write\n", psd);
    size_t length;
    zx_status_t status;

    if ((status = pty_server_send(&psd->srv, buf, count, false, &length)) < 0) {
        return status;
    } else {
        *actual = length;
        return ZX_OK;
    }
}

zx_status_t psd_ClrSetFeature(void* ctx, uint32_t clr, uint32_t set, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceClrSetFeature_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

zx_status_t psd_GetWindowSize(void* ctx, fidl_txn_t* txn) {
    fuchsia_hardware_pty_WindowSize wsz = {
        .width = 0,
        .height = 0
    };
    return fuchsia_hardware_pty_DeviceGetWindowSize_reply(txn, ZX_ERR_NOT_SUPPORTED, &wsz);
}

zx_status_t psd_MakeActive(void* ctx, uint32_t client_pty_id, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceMakeActive_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t psd_ReadEvents(void* ctx, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceReadEvents_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

zx_status_t psd_SetWindowSize(void* ctx, const fuchsia_hardware_pty_WindowSize* size,
                              fidl_txn_t* txn) {
    auto psd = static_cast<pty_server_dev_t*>(ctx);
    zxlogf(TRACE, "PTY Server Device %p message: set window size\n", psd);
    pty_server_set_window_size(&psd->srv, size->width, size->height);
    return fuchsia_hardware_pty_DeviceSetWindowSize_reply(txn, ZX_OK);
}


static fuchsia_hardware_pty_Device_ops_t psd_fidl_ops = {
    .OpenClient = pty_server_fidl_OpenClient,
    .ClrSetFeature = psd_ClrSetFeature,
    .GetWindowSize = psd_GetWindowSize,
    .MakeActive = psd_MakeActive,
    .ReadEvents = psd_ReadEvents,
    .SetWindowSize = psd_SetWindowSize
};

zx_status_t psd_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_Device_dispatch(ctx, txn, msg, &psd_fidl_ops);
}

// Since we have no special functionality,
// we just use the implementations from pty-core
// directly.
static zx_protocol_device_t psd_ops = []() {
    zx_protocol_device_t ops = {};
    ops.version = DEVICE_OPS_VERSION;
    // ops.open = default, allow cloning;
    ops.release = pty_server_release;
    ops.read = psd_read;
    ops.write = psd_write;
    ops.message = psd_message;
    return ops;
}();

// ptmx device - used to obtain the pty server of a new pty instance

static zx_status_t ptmx_open(void* ctx, zx_device_t** out, uint32_t flags) {
    zxlogf(TRACE, "PTMX open\n");
    auto psd = static_cast<pty_server_dev_t*>(calloc(1, sizeof(pty_server_dev_t)));
    if (!psd) {
        zxlogf(ERROR, "No memory for pty server device\n");
        return ZX_ERR_NO_MEMORY;
    }

    pty_server_init(&psd->srv);
    psd->srv.recv = psd_recv;
    psd->srv.set_window_size = psd_SetWindowSize;
    mtx_init(&psd->lock, mtx_plain);
    psd->fifo.head = 0;
    psd->fifo.tail = 0;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "pty";
    args.ctx = psd;
    args.ops = &psd_ops;
    args.proto_id = ZX_PROTOCOL_PTY;
    args.flags = DEVICE_ADD_INSTANCE;

    zx_status_t status = device_add(pty_root, &args, &psd->srv.zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to add PTMX device: %s\n", zx_status_get_string(status));
        free(psd);
        return status;
    }

    *out = psd->srv.zxdev;
    return ZX_OK;
}

static zx_protocol_device_t ptmx_ops = []() {
    zx_protocol_device_t ops = {};
    ops.version = DEVICE_OPS_VERSION;
    ops.open = ptmx_open;
    return ops;
}();

static zx_status_t ptmx_bind(void* ctx, zx_device_t* parent) {
    zxlogf(TRACE, "PTMX bind\n");
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "ptmx";
    args.ops = &ptmx_ops;

    zx_status_t status = device_add(parent, &args, &pty_root);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to bind PTMX device: %s\n", zx_status_get_string(status));
    }

    return status;
}

static constexpr zx_driver_ops_t ptmx_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = ptmx_bind;
    return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(ptmx, ptmx_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ptmx)
