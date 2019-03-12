// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>

#include "../shared/async-loop-owned-rpc-handler.h"
#include "coordinator.h"

namespace devmgr {

// We expect svchost to be acting as a proxy for us, when it receives a
// request for a service we host it will forward the requesting channel to us,
// we will then connect to that channel and handle fidl requests on it.
class FidlProxyHandler : public AsyncLoopOwnedRpcHandler<FidlProxyHandler> {
public:
    explicit FidlProxyHandler(Coordinator* coordinator) : coordinator_(coordinator) {}

    // This will create a proxy handler that will be owned by the async loop and
    // clean itself up.
    static zx_status_t Create(Coordinator* coordinator, async_dispatcher_t* dispatcher,
                              zx::channel proxy_channel);

    static void HandleRpc(fbl::unique_ptr<FidlProxyHandler> connection,
                          async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

    void HandleClient(async_dispatcher_t* dispatcher, zx_handle_t channel);

private:
    Coordinator* coordinator_;
};

}  // namespace devmgr

