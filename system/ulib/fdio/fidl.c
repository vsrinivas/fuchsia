// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <zircon/assert.h>
#include <zircon/device/device.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>

#include "private-fidl.h"

static zx_status_t txn_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    zxfidl_connection_t* cnxn = (void*) txn;
    fidl_message_header_t* hdr = msg->bytes;
    hdr->txid = cnxn->txid;
    return zx_channel_write(cnxn->channel, 0, msg->bytes, msg->num_bytes,
                            msg->handles, msg->num_handles);
};

// Don't actually send anything on a channel when completing this operation.
// This is useful for mocking out "close" requests.
static zx_status_t txn_null_reply(fidl_txn_t* reply, const fidl_msg_t* msg) {
    return ZX_OK;
}

static zx_status_t handle_rpc_close(zxfidl_cb_t cb, void* cookie) {
    fuchsia_io_NodeCloseRequest request;
    memset(&request, 0, sizeof(request));
    request.hdr.ordinal = fuchsia_io_NodeCloseOrdinal;
    fidl_msg_t msg = {
        .bytes = &request,
        .handles = NULL,
        .num_bytes = sizeof(request),
        .num_handles = 0u,
    };

    zxfidl_connection_t cnxn = {
        .txn = {
            .reply = txn_null_reply,
        },
        .channel = ZX_HANDLE_INVALID,
        .txid = 0,
    };

    // Remote side was closed.
    cb(&msg, &cnxn.txn, cookie);
    return ERR_DISPATCHER_DONE;
}

static zx_status_t handle_rpc(zx_handle_t h, zxfidl_cb_t cb, void* cookie) {
    uint8_t bytes[ZXFIDL_MAX_MSG_BYTES];
    zx_handle_t handles[ZXFIDL_MAX_MSG_HANDLES];
    fidl_msg_t msg = {
        .bytes = bytes,
        .handles = handles,
        .num_bytes = 0,
        .num_handles = 0,
    };

    zx_status_t r = zx_channel_read(h, 0, bytes, handles, countof(bytes),
                                    countof(handles), &msg.num_bytes,
                                    &msg.num_handles);
    if (r != ZX_OK) {
        return r;
    }

    if (msg.num_bytes < sizeof(fidl_message_header_t)) {
        zx_handle_close_many(msg.handles, msg.num_handles);
        return ZX_ERR_IO;
    }

    fidl_message_header_t* hdr = msg.bytes;
    zxfidl_connection_t cnxn = {
        .txn = {
            .reply = txn_reply,
        },
        .channel = h,
        .txid = hdr->txid,
    };

    // Callback is responsible for decoding the message, and closing
    // any associated handles.
    return cb(&msg, &cnxn.txn, cookie);
}

__EXPORT
zx_status_t zxfidl_handler(zx_handle_t h, zxfidl_cb_t cb, void* cookie) {
    if (h == ZX_HANDLE_INVALID) {
        return handle_rpc_close(cb, cookie);
    } else {
        ZX_ASSERT(zx_object_get_info(h, ZX_INFO_HANDLE_VALID, NULL, 0,
                                     NULL, NULL) == ZX_OK);
        return handle_rpc(h, cb, cookie);
    }
}

zx_status_t fidl_ioctl(zx_handle_t h, uint32_t op, const void* in_buf,
                       size_t in_len, void* out_buf, size_t out_len,
                       size_t* out_actual) {
    size_t in_handle_count = 0;
    size_t out_handle_count = 0;
    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        out_handle_count = 1;
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        out_handle_count = 2;
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        out_handle_count = 3;
        break;
    case IOCTL_KIND_SET_HANDLE:
        in_handle_count = 1;
        break;
    case IOCTL_KIND_SET_TWO_HANDLES:
        in_handle_count = 2;
        break;
    }

    if (in_len < in_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (out_len < out_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t hbuf[out_handle_count];
    size_t out_handle_actual;
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeIoctl(h, op,
                                          out_len, (zx_handle_t*) in_buf,
                                          in_handle_count, in_buf,
                                          in_len, &status, hbuf,
                                          out_handle_count, &out_handle_actual,
                                          out_buf, out_len, out_actual)) != ZX_OK) {
        return io_status;
    }

    if (status != ZX_OK) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return status;
    }
    if (out_handle_actual != out_handle_count) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return ZX_ERR_IO;
    }

    memcpy(out_buf, hbuf, out_handle_count * sizeof(zx_handle_t));
    return ZX_OK;
}
