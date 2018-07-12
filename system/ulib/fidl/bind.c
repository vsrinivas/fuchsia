// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/wait.h>
#include <lib/fidl/bind.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

typedef struct fidl_binding {
    async_wait_t wait;
    fidl_dispatch_t* dispatch;
    void* ctx;
    const void* ops;
} fidl_binding_t;

typedef struct fidl_connection {
    fidl_txn_t txn;
    zx_handle_t channel;
    zx_txid_t txid;
} fidl_connection_t;

static zx_status_t fidl_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    fidl_connection_t* conn = (fidl_connection_t*)txn;
    if (conn->txid == 0u)
        return ZX_ERR_BAD_STATE;
    if (msg->num_bytes < sizeof(fidl_message_header_t))
        return ZX_ERR_INVALID_ARGS;
    fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
    hdr->txid = conn->txid;
    conn->txid = 0u;
    return zx_channel_write(conn->channel, 0, msg->bytes, msg->num_bytes,
                            msg->handles, msg->num_handles);
}

static void fidl_message_handler(async_dispatcher_t* dispatcher,
                                 async_wait_t* wait,
                                 zx_status_t status,
                                 const zx_packet_signal_t* signal) {
    fidl_binding_t* binding = (fidl_binding_t*)wait;
    if (status != ZX_OK)
        goto shutdown;

    if (signal->observed & ZX_CHANNEL_READABLE) {
        char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
        zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
        for (uint64_t i = 0; i < signal->count; i++) {
            fidl_msg_t msg = {
                .bytes = bytes,
                .handles = handles,
                .num_bytes = 0u,
                .num_handles = 0u,
            };
            status = zx_channel_read(wait->object, 0, bytes, handles,
                                     ZX_CHANNEL_MAX_MSG_BYTES,
                                     ZX_CHANNEL_MAX_MSG_HANDLES,
                                     &msg.num_bytes, &msg.num_handles);
            if (status == ZX_ERR_SHOULD_WAIT)
                break;
            if (status != ZX_OK || msg.num_bytes < sizeof(fidl_message_header_t))
                goto shutdown;
            fidl_message_header_t* hdr = (fidl_message_header_t*)msg.bytes;
            fidl_connection_t conn = {
                .txn.reply = fidl_reply,
                .channel = wait->object,
                .txid = hdr->txid,
            };
            status = binding->dispatch(binding->ctx, &conn.txn, &msg, binding->ops);
            if (status != ZX_OK)
                goto shutdown;
        }
        status = async_begin_wait(dispatcher, wait);
        if (status != ZX_OK)
            goto shutdown;
        return;
    }

shutdown:
    zx_handle_close(wait->object);
    free(binding);
}

zx_status_t fidl_bind(async_dispatcher_t* dispatcher, zx_handle_t channel,
                      fidl_dispatch_t* dispatch, void* ctx, const void* ops) {
    fidl_binding_t* binding = calloc(1, sizeof(fidl_binding_t));
    binding->wait.handler = fidl_message_handler;
    binding->wait.object = channel;
    binding->wait.trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    binding->dispatch = dispatch;
    binding->ctx = ctx;
    binding->ops = ops;
    zx_status_t status = async_begin_wait(dispatcher, &binding->wait);
    if (status != ZX_OK) {
        zx_handle_close(binding->wait.object);
        free(binding);
    }
    return status;
}
