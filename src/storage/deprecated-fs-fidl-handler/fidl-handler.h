// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/limits.h>
#include <stdint.h>

#include <type_traits>

#include <fbl/function.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

// Fuchsia-io limits.
//
// TODO(fxbug.dev/7464): Compute these values with the "union of all fuchsia-io"
// messages.
#define ZXFIDL_MAX_MSG_BYTES (FDIO_CHUNK_SIZE * 2)
#define ZXFIDL_MAX_MSG_HANDLES (16)

// indicates the callback is taking responsibility for the
// channel receiving incoming messages.
//
// Unlike ERR_DISPATCHER_INDIRECT, this callback is propagated
// through ReadMessage.
#define ERR_DISPATCHER_ASYNC ZX_ERR_ASYNC

// indicates that this was a close message and that no further
// callbacks should be made to the dispatcher
#define ERR_DISPATCHER_DONE ZX_ERR_STOP

namespace fs {

// FidlConnection contains enough context to respond to a FIDL message.
//
// It contains both the underlying fidl transaction, as well as the channel and txid,
// which are necessary for responding to fidl messages.
class FidlConnection {
 public:
  // TODO(smklein): convert channel to a zx::unowned_channel.
  FidlConnection(fidl_txn_t txn, zx_handle_t channel, zx_txid_t txid)
      : txn_(std::move(txn)), channel_(std::move(channel)), txid_(std::move(txid)) {}

  fidl_txn_t* Txn() { return &txn_; }

  zx_txid_t Txid() const { return txid_; }

  zx_handle_t Channel() const { return channel_; }

  // Utilizes a |fidl_txn_t| object as a wrapped FidlConnection.
  //
  // Only safe to call if |txn| was previously returned by |FidlConnection.Txn()|.
  static const FidlConnection* FromTxn(const fidl_txn_t* txn);

  // Copies txn into a new FidlConnection.
  //
  // This may be useful for copying a FidlConnection out of stack-allocated scope,
  // so a response may be generated asynchronously.
  //
  // Only safe to call if |txn| was previously returned by |FidlConnection.Txn()|.
  static FidlConnection CopyTxn(const fidl_txn_t* txn);

 private:
  fidl_txn_t txn_;
  zx_handle_t channel_;
  zx_txid_t txid_;
};

inline const FidlConnection* FidlConnection::FromTxn(const fidl_txn_t* txn) {
  static_assert(std::is_standard_layout<FidlConnection>::value,
                "Cannot cast from non-standard layout class");
  static_assert(offsetof(FidlConnection, txn_) == 0, "FidlConnection must be convertable to txn");
  return reinterpret_cast<const FidlConnection*>(txn);
}

inline FidlConnection FidlConnection::CopyTxn(const fidl_txn_t* txn) {
  static_assert(std::is_trivially_copyable<FidlConnection>::value, "Cannot trivially copy");
  return *FromTxn(txn);
}

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
using FidlDispatchFunction = fbl::Function<zx_status_t(fidl_msg_t* msg, FidlConnection* txn)>;

// Attempts to read and dispatch a FIDL message.
//
// If a message cannot be read, returns an error instead of blocking.
zx_status_t ReadMessage(zx_handle_t h, FidlDispatchFunction dispatch);

// Synthesizes a FIDL close message.
//
// This may be invoked when a channel is closed, to simulate dispatching
// to the same close function.
zx_status_t CloseMessage(FidlDispatchFunction dispatch);

}  // namespace fs
