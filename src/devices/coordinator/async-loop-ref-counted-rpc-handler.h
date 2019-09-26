// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_ASYNC_LOOP_REF_COUNTED_RPC_HANDLER_H_
#define SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_ASYNC_LOOP_REF_COUNTED_RPC_HANDLER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include <utility>

#include <fbl/ref_ptr.h>

namespace devmgr {

// Mixin for representing a type that represents an RPC handler and that has a
// reference owned by an async loop.  The loop will own both the wrapped type and the RPC
// connection handle.
//
// Deriving classes should define and implement this function:
// static void HandleRpc(fbl::RefPtr<T>&& conn,
//                       async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
//                       const zx_packet_signal_t* signal);
template <typename T>
class AsyncLoopRefCountedRpcHandler {
 public:
  ~AsyncLoopRefCountedRpcHandler() {
    zx_status_t status = wait_.Cancel();
    ZX_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);

    zx_handle_close(wait_.object());
  }

  // Begins waiting in |dispatcher| on |conn->wait|.  This transfers ownership
  // of a reference to |conn| to the dispatcher.  The dispatcher returns ownership when the
  // handler is invoked.
  static zx_status_t BeginWait(fbl::RefPtr<T> conn, async_dispatcher_t* dispatcher) {
    zx_status_t status = conn->wait_.Begin(dispatcher);
    if (status == ZX_OK) {
      // This reference will be recovered by MakeRefPtrNoAdopt in
      // HandleRpcEntry
      __UNUSED auto ptr = fbl::ExportToRawPtr(&conn);
    }
    return status;
  }

  // Entrypoint for the RPC handler that captures the pointer ownership
  // semantics.
  void HandleRpcEntry(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal) {
    auto self = fbl::ImportFromRawPtr(static_cast<T*>(this));
    T::HandleRpc(std::move(self), dispatcher, wait, status, signal);
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(wait_.object()); }

  // Sets the channel to the given handle and returns the old value
  zx::channel set_channel(zx::channel h) {
    zx::channel old(wait_.object());
    wait_.set_object(h.release());
    return old;
  }

  using WaitType = async::WaitMethod<AsyncLoopRefCountedRpcHandler<T>,
                                     &AsyncLoopRefCountedRpcHandler<T>::HandleRpcEntry>;

 private:
  WaitType wait_{this, ZX_HANDLE_INVALID, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED};
};

}  // namespace devmgr

#endif  // SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_ASYNC_LOOP_REF_COUNTED_RPC_HANDLER_H_
