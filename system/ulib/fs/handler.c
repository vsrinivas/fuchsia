// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/handler.h>

#include <stdlib.h>
#include <string.h>
#include <fuchsia/io/c/fidl.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static zx_status_t txn_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    vfs_connection_t* cnxn = (void*) txn;
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

static zx_status_t handle_rpc_close(vfs_cb_t cb, void* cookie) {
    fuchsia_io_NodeCloseRequest request;
    memset(&request, 0, sizeof(request));
    request.hdr.ordinal = fuchsia_io_NodeCloseOrdinal;
    fidl_msg_t msg = {
        .bytes = &request,
        .handles = NULL,
        .num_bytes = sizeof(request),
        .num_handles = 0u,
    };

    vfs_connection_t cnxn = {
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

static zx_status_t handle_rpc(zx_handle_t h, vfs_cb_t cb, void* cookie) {
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
    vfs_connection_t cnxn = {
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

zx_status_t vfs_handler(zx_handle_t h, vfs_cb_t cb, void* cookie) {
    if (h == ZX_HANDLE_INVALID) {
        return handle_rpc_close(cb, cookie);
    } else {
        ZX_ASSERT(zx_object_get_info(h, ZX_INFO_HANDLE_VALID, NULL, 0,
                                     NULL, NULL) == ZX_OK);
        return handle_rpc(h, cb, cookie);
    }
}
