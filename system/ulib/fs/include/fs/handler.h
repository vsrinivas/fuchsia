// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/limits.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Fuchsia-io limits.
//
// TODO(FIDL-127): Compute these values with the "union of all fuchsia-io"
// messages.
#define ZXFIDL_MAX_MSG_BYTES    (FDIO_CHUNK_SIZE * 2)
#define ZXFIDL_MAX_MSG_HANDLES  (FDIO_MAX_HANDLES)

// indicates the callback is taking responsibility for the
// channel receiving incoming messages.
//
// Unlike ERR_DISPATCHER_INDIRECT, this callback is propagated
// through the vfs_handlers.
#define ERR_DISPATCHER_ASYNC ZX_ERR_ASYNC

// indicates that this was a close message and that no further
// callbacks should be made to the dispatcher
#define ERR_DISPATCHER_DONE ZX_ERR_STOP

// callback to process a FIDL message.
// - |msg| is a decoded FIDL message.
// - return value of ERR_DISPATCHER_{INDIRECT,ASYNC} indicates that the reply is
//   being handled by the callback (forwarded to another server, sent later,
//   etc, and no reply message should be sent).
// - WARNING: Once this callback returns, usage of |msg| is no longer
//   valid. If a client transmits ERR_DISPATCHER_{INDIRECT,ASYNC}, and intends
//   to respond asynchronously, they must copy the fields of |msg| they
//   wish to use at a later point in time.
// - otherwise, the return value is treated as the status to send
//   in the rpc response, and msg.len indicates how much valid data
//   to send.  On error return msg.len will be set to 0.
typedef zx_status_t (*vfs_cb_t)(fidl_msg_t* msg, fidl_txn_t* txn,
                                   void* cookie);

//TODO: this really should be private to fidl.c, but is used by libfs
typedef struct vfs_connection {
    fidl_txn_t txn;
    zx_handle_t channel;
    zx_txid_t txid;
} vfs_connection_t;

static_assert(offsetof(vfs_connection_t, txn) == 0,
              "Connection must transparently be a fidl_txn");

inline vfs_connection_t vfs_txn_copy(fidl_txn_t* txn) {
    return *(vfs_connection_t*) txn;
}

// A fdio_dispatcher_handler suitable for use with a fdio_dispatcher.
zx_status_t vfs_handler(zx_handle_t h, vfs_cb_t cb, void* cookie);

__END_CDECLS
