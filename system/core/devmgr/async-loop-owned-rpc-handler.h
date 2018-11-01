// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/cpp/wait.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/channel.h>

namespace devmgr {

// Mixin for representing a type that represents an RPC handler and is owned
// by an async loop.  The loop will own both the wrapped type and the RPC
// connection handle.
//
// Deriving classes should define and implement this function:
// static void HandleRpc(fbl::unique_ptr<T> conn,
//                      async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
//                      const zx_packet_signal_t* signal);
template <typename T> class AsyncLoopOwnedRpcHandler {
public:
    ~AsyncLoopOwnedRpcHandler() {
        zx_status_t status = wait_.Cancel();
        ZX_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);

        zx_handle_close(wait_.object());
    }

    // Begins waiting in |dispatcher| on |conn->wait|.  This transfers ownership
    // of |conn| to the dispatcher.  The dispatcher returns ownership when the
    // handler is invoked.
    static zx_status_t BeginWait(fbl::unique_ptr<T> conn,
                                 async_dispatcher_t* dispatcher) {
        zx_status_t status = conn->wait_.Begin(dispatcher);
        if (status == ZX_OK) {
            __UNUSED auto ptr = conn.release();
        }
        return status;
    }

    // Entrypoint for the RPC handler that captures the pointer ownership
    // semantics.
    void HandleRpcEntry(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
        fbl::unique_ptr<T> self(static_cast<T*>(this));
        T::HandleRpc(fbl::move(self), dispatcher, wait, status, signal);
    }

    zx::unowned_channel channel() {
        return zx::unowned_channel(wait_.object());
    }

    void set_channel(zx::channel h) {
        if (wait_.object() != ZX_HANDLE_INVALID) {
            zx_handle_close(wait_.object());
        }
        wait_.set_object(h.release());
    }

    using WaitType = async::WaitMethod<AsyncLoopOwnedRpcHandler<T>,
          &AsyncLoopOwnedRpcHandler<T>::HandleRpcEntry>;
private:
    WaitType wait_{this, ZX_HANDLE_INVALID, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED};
};

} // namespace devmgr
