// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl-handler.h"

#include <fuchsia/io/c/fidl.h>
#include <lib/fidl/txn_header.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace fs {
namespace {

zx_status_t Reply(fidl_txn_t* txn, const fidl_outgoing_msg_t* msg) {
  auto connection = FidlConnection::FromTxn(txn);
  auto header = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  header->txid = connection->Txid();
  return zx_channel_write(connection->Channel(), 0, msg->bytes, msg->num_bytes, msg->handles,
                          msg->num_handles);
}

// Don't actually send anything on a channel when completing this operation.
// This is useful for mocking out "close" requests.
zx_status_t NullReply(fidl_txn_t* reply, const fidl_outgoing_msg_t* msg) { return ZX_OK; }

}  // namespace

zx_status_t ReadMessage(zx_handle_t h, FidlDispatchFunction dispatch) {
  ZX_ASSERT(zx_object_get_info(h, ZX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL) == ZX_OK);
  uint8_t bytes[ZXFIDL_MAX_MSG_BYTES];
  zx_handle_t handles[ZXFIDL_MAX_MSG_HANDLES];
  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .handles = handles,
      .num_bytes = 0,
      .num_handles = 0,
  };

  zx_status_t r = zx_channel_read(h, 0, bytes, handles, countof(bytes), countof(handles),
                                  &msg.num_bytes, &msg.num_handles);
  if (r != ZX_OK) {
    return r;
  }

  if (msg.num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg.handles, msg.num_handles);
    return ZX_ERR_IO;
  }

  auto header = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  fidl_txn_t txn = {
      .reply = Reply,
  };
  FidlConnection connection(std::move(txn), h, header->txid);

  // Callback is responsible for decoding the message, and closing
  // any associated handles.
  return dispatch(&msg, &connection);
}

zx_status_t CloseMessage(FidlDispatchFunction dispatch) {
  fuchsia_io_NodeCloseRequest request;
  memset(&request, 0, sizeof(request));
  fidl_init_txn_header(&request.hdr, 0, fuchsia_io_NodeCloseOrdinal);
  fidl_incoming_msg_t msg = {
      .bytes = &request,
      .handles = NULL,
      .num_bytes = sizeof(request),
      .num_handles = 0u,
  };

  fidl_txn_t txn = {
      .reply = NullReply,
  };
  FidlConnection connection(std::move(txn), ZX_HANDLE_INVALID, 0);

  // Remote side was closed.
  dispatch(&msg, &connection);
  return ERR_DISPATCHER_DONE;
}

}  // namespace fs
