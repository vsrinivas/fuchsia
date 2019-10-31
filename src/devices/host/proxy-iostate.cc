// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "proxy-iostate.h"

#include <memory>

#include <fbl/auto_lock.h>

#include "connection-destroyer.h"
#include "log.h"
#include "zx-device.h"

namespace devmgr {

ProxyIostate::~ProxyIostate() {
  fbl::AutoLock guard(&dev->proxy_ios_lock);
  ZX_ASSERT(dev->proxy_ios != this);
}

// Handling RPC From Proxy Devices to BusDevs
void ProxyIostate::HandleRpc(std::unique_ptr<ProxyIostate> conn, async_dispatcher_t* dispatcher,
                             async::WaitBase* wait, zx_status_t status,
                             const zx_packet_signal_t* signal) {
  auto handle_destroy = [&conn]() {
    fbl::AutoLock guard(&conn->dev->proxy_ios_lock);
    // If proxy_ios is not |conn|, then it's had a packet queued already to
    // destroy it, so we should let the queued destruction handle things.
    // Otherwise we should destroy it.
    if (conn->dev->proxy_ios == conn.get()) {
      // Mark proxy_ios as disconnected, so that CancelLocked doesn't try to
      // destroy it too
      conn->dev->proxy_ios = nullptr;
      // The actual destruction will happen when |conn| goes out of scope.
    } else {
      __UNUSED auto ptr = conn.release();
    }
  };
  if (status != ZX_OK) {
    return handle_destroy();
  }

  if (conn->dev == nullptr) {
    log(RPC_SDW, "proxy-rpc: stale rpc? (ios=%p)\n", conn.get());
    // Do not re-issue the wait here
    return handle_destroy();
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    log(RPC_SDW, "proxy-rpc: rpc readable (ios=%p,dev=%p)\n", conn.get(), conn->dev.get());
    zx_status_t r = conn->dev->ops->rxrpc(conn->dev->ctx, wait->object());
    if (r != ZX_OK) {
      log(RPC_SDW, "proxy-rpc: rpc cb error %d (ios=%p,dev=%p)\n", r, conn.get(), conn->dev.get());
      return handle_destroy();
    }
    BeginWait(std::move(conn), dispatcher);
    return;
  }
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    log(RPC_SDW, "proxy-rpc: peer closed (ios=%p,dev=%p)\n", conn.get(), conn->dev.get());
    return handle_destroy();
  }
  log(ERROR, "devhost: no work? %08x\n", signal->observed);
  BeginWait(std::move(conn), dispatcher);
}

zx_status_t ProxyIostate::Create(const fbl::RefPtr<zx_device_t>& dev, zx::channel rpc,
                                 async_dispatcher_t* dispatcher) {
  // This must be held for the adding of the channel to the port, since the
  // async loop may run immediately after that point.
  fbl::AutoLock guard(&dev->proxy_ios_lock);

  if (dev->proxy_ios) {
    dev->proxy_ios->CancelLocked(dispatcher);
  }

  auto ios = std::make_unique<ProxyIostate>(dev);
  if (ios == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  ios->set_channel(std::move(rpc));

  // |ios| is will be owned by the async loop.  |dev| holds a reference that will be
  // cleared prior to destruction.
  dev->proxy_ios = ios.get();

  zx_status_t status = BeginWait(std::move(ios), dispatcher);
  if (status != ZX_OK) {
    dev->proxy_ios = nullptr;
    return status;
  }

  return ZX_OK;
}

void ProxyIostate::CancelLocked(async_dispatcher_t* dispatcher) {
  ZX_ASSERT(this->dev->proxy_ios == this);
  this->dev->proxy_ios = nullptr;
  // TODO(teisenbe): We should probably check the return code in case the
  // queue was full
  ConnectionDestroyer::Get()->QueueProxyConnection(dispatcher, this);
}

}  // namespace devmgr
