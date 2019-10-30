// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_ASYNC_LOOP_OWNED_RPC_HANDLER_H_
#define SRC_DEVICES_HOST_ASYNC_LOOP_OWNED_RPC_HANDLER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include <memory>
#include <utility>

namespace devmgr {

// Mixin for representing a type that represents an RPC handler and is owned
// by an async loop.  The loop will own both the wrapped type and the RPC
// connection handle.
//
// Deriving classes should define and implement this function:
// static void HandleRpc(std::unique_ptr<T> conn,
//                      async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
//                      const zx_packet_signal_t* signal);
template <typename T>
class AsyncLoopOwnedRpcHandler {
 public:
  ~AsyncLoopOwnedRpcHandler() {
    zx_status_t status = wait_.Cancel();
    ZX_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);

    zx_handle_close(wait_.object());
  }

  // Variant of BeginWait that conditionally consumes |conn|.  On failure,
  // |*conn| is untouched.
  static zx_status_t BeginWait(std::unique_ptr<T>* conn, async_dispatcher_t* dispatcher) {
    zx_status_t status = (*conn)->wait_.Begin(dispatcher);
    if (status == ZX_OK) {
      __UNUSED auto ptr = conn->release();
    }
    return status;
  }

  // Begins waiting in |dispatcher| on |conn->wait|.  This transfers ownership
  // of |conn| to the dispatcher.  The dispatcher returns ownership when the
  // handler is invoked.
  static zx_status_t BeginWait(std::unique_ptr<T> conn, async_dispatcher_t* dispatcher) {
    return BeginWait(&conn, dispatcher);
  }

  // Entrypoint for the RPC handler that captures the pointer ownership
  // semantics.
  void HandleRpcEntry(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal) {
    std::unique_ptr<T> self(static_cast<T*>(this));
    T::HandleRpc(std::move(self), dispatcher, wait, status, signal);
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(wait_.object()); }

  // Sets the channel to the given handle and returns the old value
  zx::channel set_channel(zx::channel h) {
    zx::channel old(wait_.object());
    wait_.set_object(h.release());
    return old;
  }

  using WaitType =
      async::WaitMethod<AsyncLoopOwnedRpcHandler<T>, &AsyncLoopOwnedRpcHandler<T>::HandleRpcEntry>;

 private:
  WaitType wait_{this, ZX_HANDLE_INVALID, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED};
};

}  // namespace devmgr

#endif  // SRC_DEVICES_HOST_ASYNC_LOOP_OWNED_RPC_HANDLER_H_
