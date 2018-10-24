// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/wait.h>
#include <lib/fidl-async/bind.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

typedef struct fidl_binding {
    async_wait_t wait;
    fidl_dispatch_t* dispatch;
    async_dispatcher_t* dispatcher;
    void* ctx;
    const void* ops;
} fidl_binding_t;

typedef struct fidl_connection {
    fidl_txn_t txn;
    zx_handle_t channel;
    zx_txid_t txid;
    fidl_binding_t* binding;
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

static void fidl_binding_destroy(fidl_binding_t* binding) {
    zx_handle_close(binding->wait.object);
    free(binding);
}

static void fidl_message_handler(async_dispatcher_t* dispatcher,
                                 async_wait_t* wait,
                                 zx_status_t status,
                                 const zx_packet_signal_t* signal) {
    fidl_binding_t* binding = (fidl_binding_t*)wait;
    if (status != ZX_OK) {
        goto shutdown;
    }

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
            if (status == ZX_ERR_SHOULD_WAIT) {
                break;
            }
            if (status != ZX_OK || msg.num_bytes < sizeof(fidl_message_header_t)) {
                goto shutdown;
            }
            fidl_message_header_t* hdr = (fidl_message_header_t*)msg.bytes;
            fidl_connection_t conn = {
                .txn.reply = fidl_reply,
                .channel = wait->object,
                .txid = hdr->txid,
                .binding = binding,
            };
            status = binding->dispatch(binding->ctx, &conn.txn, &msg, binding->ops);
            switch (status) {
            case ZX_OK:
                status = async_begin_wait(dispatcher, wait);
                if (status != ZX_OK) {
                    goto shutdown;
                }
                return;
            case ZX_ERR_ASYNC:
                return;
            default:
                goto shutdown;
            }
        }
    }

shutdown:
    fidl_binding_destroy(binding);
}

zx_status_t fidl_bind(async_dispatcher_t* dispatcher, zx_handle_t channel,
                      fidl_dispatch_t* dispatch, void* ctx, const void* ops) {
    fidl_binding_t* binding = calloc(1, sizeof(fidl_binding_t));
    binding->wait.handler = fidl_message_handler;
    binding->wait.object = channel;
    binding->wait.trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    binding->dispatch = dispatch;
    binding->dispatcher = dispatcher;
    binding->ctx = ctx;
    binding->ops = ops;
    zx_status_t status = async_begin_wait(dispatcher, &binding->wait);
    if (status != ZX_OK) {
        fidl_binding_destroy(binding);
    }
    return status;
}

typedef struct fidl_async_txn {
    fidl_connection_t connection;
} fidl_async_txn_t;

fidl_async_txn_t* fidl_async_txn_create(fidl_txn_t* txn) {
    fidl_connection_t* connection = (fidl_connection_t*) txn;

    fidl_async_txn_t* async_txn = calloc(1, sizeof(fidl_async_txn_t));
    memcpy(&async_txn->connection, connection, sizeof(*connection));

    return async_txn;
}

fidl_txn_t* fidl_async_txn_borrow(fidl_async_txn_t* async_txn) {
    return &async_txn->connection.txn;
}

zx_status_t fidl_async_txn_complete(fidl_async_txn_t* async_txn, bool rebind) {
    zx_status_t status = ZX_OK;
    if (rebind) {
        status = async_begin_wait(async_txn->connection.binding->dispatcher,
                                  &async_txn->connection.binding->wait);
        if (status == ZX_OK) {
            return ZX_OK;
        }
    }

    fidl_binding_destroy(async_txn->connection.binding);
    free(async_txn);
    return status;
}
