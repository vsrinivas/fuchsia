// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include "../shared/async-loop-owned-rpc-handler.h"

struct zx_device;

namespace devmgr {

struct ProxyIostate : AsyncLoopOwnedRpcHandler<ProxyIostate> {
    ProxyIostate() = default;
    ~ProxyIostate();

    // Creates a ProxyIostate and points |dev| at it.  The ProxyIostate is owned
    // by the async loop, and its destruction may be requested by calling
    // Cancel().
    static zx_status_t Create(const fbl::RefPtr<zx_device>& dev, zx::channel rpc,
                              async_dispatcher_t* dispatcher);

    // Request the destruction of the proxy connection
    void Cancel(async_dispatcher_t* dispatcher);

    static void HandleRpc(fbl::unique_ptr<ProxyIostate> conn, async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

    fbl::RefPtr<zx_device> dev;
};
static void proxy_ios_destroy(const fbl::RefPtr<zx_device>& dev);

} // namespace devmgr
