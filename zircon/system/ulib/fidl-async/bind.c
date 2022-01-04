// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/wait.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl/trace.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>
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

static zx_status_t fidl_reply(fidl_txn_t* txn, const fidl_outgoing_msg_t* msg) {
  fidl_connection_t* conn = (fidl_connection_t*)txn;
  if (conn->txid == 0u)
    return ZX_ERR_BAD_STATE;
  switch (msg->type) {
    case FIDL_OUTGOING_MSG_TYPE_BYTE: {
      if (msg->byte.num_bytes < sizeof(fidl_message_header_t))
        return ZX_ERR_INVALID_ARGS;
      fidl_message_header_t* hdr = (fidl_message_header_t*)msg->byte.bytes;
      hdr->txid = conn->txid;
      conn->txid = 0u;
      zx_handle_disposition_t handle_dispositions[ZX_CHANNEL_MAX_MSG_HANDLES];
      fidl_channel_handle_metadata_t* metadata =
          (fidl_channel_handle_metadata_t*)(msg->byte.handle_metadata);
      for (uint32_t i = 0; i < msg->byte.num_handles; i++) {
        handle_dispositions[i].operation = ZX_HANDLE_OP_MOVE;
        handle_dispositions[i].handle = msg->byte.handles[i];
        handle_dispositions[i].type = metadata[i].obj_type;
        handle_dispositions[i].rights = metadata[i].rights;
        handle_dispositions[i].result = ZX_OK;
      }
      fidl_trace(WillCChannelWrite, NULL /* type */, msg->byte.bytes, msg->byte.num_bytes,
                 msg->byte.num_handles);
      const zx_status_t status =
          zx_channel_write_etc(conn->channel, 0, msg->byte.bytes, msg->byte.num_bytes,
                               handle_dispositions, msg->byte.num_handles);
      fidl_trace(DidCChannelWrite);
      return status;
    }
    case FIDL_OUTGOING_MSG_TYPE_IOVEC: {
      if (msg->iovec.num_iovecs < 1)
        return ZX_ERR_INVALID_ARGS;
      // The message header must fit within the first iovec.
      if (msg->iovec.iovecs[0].capacity < sizeof(fidl_message_header_t))
        return ZX_ERR_INVALID_ARGS;
      fidl_message_header_t* hdr = (fidl_message_header_t*)msg->iovec.iovecs[0].buffer;
      hdr->txid = conn->txid;
      conn->txid = 0u;
      zx_handle_disposition_t handle_dispositions[ZX_CHANNEL_MAX_MSG_HANDLES];
      fidl_channel_handle_metadata_t* metadata =
          (fidl_channel_handle_metadata_t*)(msg->iovec.handle_metadata);
      for (uint32_t i = 0; i < msg->iovec.num_handles; i++) {
        handle_dispositions[i].operation = ZX_HANDLE_OP_MOVE;
        handle_dispositions[i].handle = msg->iovec.handles[i];
        handle_dispositions[i].type = metadata[i].obj_type;
        handle_dispositions[i].rights = metadata[i].rights;
        handle_dispositions[i].result = ZX_OK;
      }
      fidl_trace(WillCChannelWrite);
      const zx_status_t status =
          zx_channel_write_etc(conn->channel, ZX_CHANNEL_WRITE_USE_IOVEC, msg->iovec.iovecs,
                               msg->iovec.num_iovecs, handle_dispositions, msg->iovec.num_handles);
      fidl_trace(DidCChannelWrite);
      return status;
    }
    default:
      ZX_PANIC("unsupported message type");
  }
}

static void fidl_binding_destroy(fidl_binding_t* binding) {
  zx_handle_close(binding->wait.object);
  free(binding);
}

static void fidl_message_handler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
  fidl_binding_t* binding = (fidl_binding_t*)wait;
  if (status != ZX_OK) {
    goto shutdown;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint64_t i = 0; i < signal->count; i++) {
      fidl_incoming_msg_t msg = {
          .bytes = bytes,
          .handles = handles,
          .handle_metadata = (fidl_handle_metadata_t*)(handle_metadata),
          .num_bytes = 0u,
          .num_handles = 0u,
      };
      fidl_trace(WillCChannelRead);
      status = zx_channel_read_etc(wait->object, 0, bytes, handle_infos, ZX_CHANNEL_MAX_MSG_BYTES,
                                   ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
      if (status == ZX_ERR_SHOULD_WAIT) {
        // This occurs when someone else has read the message we were expecting.
        goto shutdown;
      }
      if (status != ZX_OK || msg.num_bytes < sizeof(fidl_message_header_t)) {
        goto shutdown;
      }
      fidl_trace(DidCChannelRead, NULL /* type */, msg.bytes, msg.num_bytes, msg.num_handles);
      for (uint32_t i = 0; i < msg.num_handles; i++) {
        msg.handles[i] = handle_infos[i].handle;
        fidl_channel_handle_metadata_t metadata = {
            .obj_type = handle_infos[i].type,
            .rights = handle_infos[i].rights,
        };
        handle_metadata[i] = metadata;
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
          continue;
        case ZX_ERR_ASYNC:
          return;
        default:
          goto shutdown;
      }
    }

    // Only |status| == ZX_OK will lead here
    if (async_begin_wait(dispatcher, wait) == ZX_OK) {
      return;
    } else {
      goto shutdown;
    }
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
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
  binding->wait.options = 0;
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
  fidl_connection_t* connection = (fidl_connection_t*)txn;

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
      free(async_txn);
      return ZX_OK;
    }
  }

  fidl_binding_destroy(async_txn->connection.binding);
  free(async_txn);
  return status;
}
