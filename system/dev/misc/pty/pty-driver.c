// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include "pty-core.h"
#include "pty-fifo.h"

#include <magenta/device/pty.h>

typedef struct pty_server_dev {
    pty_server_t srv;

    mtx_t lock;
    pty_fifo_t fifo;
} pty_server_dev_t;

static mx_device_t* pty_root;

#define psd_from_ps(ps) containerof(ps, pty_server_dev_t, srv)

static mx_status_t psd_recv(pty_server_t* ps, const void* data, size_t len, size_t* actual) {
    if (len == 0) {
        return 0;
    }

    pty_server_dev_t* psd = psd_from_ps(ps);

    bool was_empty = pty_fifo_is_empty(&psd->fifo);
    *actual = pty_fifo_write(&psd->fifo, data, len, false);
    if (was_empty && *actual) {
        device_state_set(ps->mxdev, DEV_STATE_READABLE);
    }

    if (*actual == 0) {
        return MX_ERR_SHOULD_WAIT;
    } else {
        return MX_OK;
    }
}

static mx_status_t psd_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    pty_server_dev_t* psd = ctx;

    bool eof = false;

    mtx_lock(&psd->srv.lock);
    bool was_full = pty_fifo_is_full(&psd->fifo);
    size_t length = pty_fifo_read(&psd->fifo, buf, count);
    if (pty_fifo_is_empty(&psd->fifo)) {
        if (list_is_empty(&psd->srv.clients)) {
            eof = true;
        } else {
            device_state_clr(psd->srv.mxdev, DEV_STATE_READABLE);
        }
    }
    if (was_full && length) {
        pty_server_resume_locked(&psd->srv);
    }
    mtx_unlock(&psd->srv.lock);

    if (length > 0) {
        *actual = length;
        return MX_OK;
    } else if (eof) {
        *actual = 0;
        return MX_OK;
    } else {
        return MX_ERR_SHOULD_WAIT;
    }
}

static mx_status_t psd_write(void* ctx, const void* buf, size_t count, mx_off_t off,
                             size_t* actual) {
    pty_server_dev_t* psd = ctx;
    size_t length;
    mx_status_t status;

    if ((status = pty_server_send(&psd->srv, buf, count, false, &length)) < 0) {
        return status;
    } else {
        *actual = length;
        return MX_OK;
    }
}

static mx_status_t psd_ioctl(void* ctx, uint32_t op,
                  const void* in_buf, size_t in_len,
                  void* out_buf, size_t out_len, size_t* out_actual) {
    pty_server_dev_t* psd = ctx;

    switch (op) {
    case IOCTL_PTY_SET_WINDOW_SIZE: {
        const pty_window_size_t* wsz = in_buf;
        if (in_len != sizeof(pty_window_size_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        pty_server_set_window_size(&psd->srv, wsz->width, wsz->height);
        return MX_OK;
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

// Since we have no special functionality,
// we just use the implementations from pty-core
// directly.
static mx_protocol_device_t psd_ops = {
    .version = DEVICE_OPS_VERSION,
    // .open = default, allow cloning
    .open_at = pty_server_openat,
    .release = pty_server_release,
    .read = psd_read,
    .write = psd_write,
    .ioctl = psd_ioctl,
};


// ptmx device - used to obtain the pty server of a new pty instance

static mx_status_t ptmx_open(void* ctx, mx_device_t** out, uint32_t flags) {
    pty_server_dev_t* psd;
    if ((psd = calloc(1, sizeof(pty_server_dev_t))) == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    pty_server_init(&psd->srv);
    psd->srv.recv = psd_recv;
    mtx_init(&psd->lock, mtx_plain);
    psd->fifo.head = 0;
    psd->fifo.tail = 0;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "pty",
        .ctx = psd,
        .ops = &psd_ops,
        .proto_id = MX_PROTOCOL_PTY,
        .flags = DEVICE_ADD_INSTANCE,
    };

    mx_status_t status;
    if ((status = device_add(pty_root, &args, &psd->srv.mxdev)) < 0) {
        free(psd);
        return status;
    }

    *out = psd->srv.mxdev;
    return MX_OK;
}

static mx_protocol_device_t ptmx_ops = {
    .version = DEVICE_OPS_VERSION,
    .open = ptmx_open,
};

static mx_status_t ptmx_bind(void* ctx, mx_device_t* parent, void** cookie) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ptmx",
        .ops = &ptmx_ops,
    };

    return device_add(parent, &args, &pty_root);
}

static mx_driver_ops_t ptmx_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ptmx_bind,
};

MAGENTA_DRIVER_BEGIN(ptmx, ptmx_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT),
MAGENTA_DRIVER_END(ptmx)
