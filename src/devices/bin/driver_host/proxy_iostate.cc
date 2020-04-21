// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "proxy_iostate.h"

#include <zircon/status.h>

#include <memory>

#include <fbl/auto_lock.h>

#include "connection_destroyer.h"
#include "src/devices/lib/log/log.h"
#include "zx_device.h"

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
    FX_VLOGF(1, "proxy-rpc", "Stale RPC, IO state %p", conn.get());
    // Do not re-issue the wait here
    return handle_destroy();
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    zx_status_t r = conn->dev->ops->rxrpc(conn->dev->ctx, wait->object());
    if (r != ZX_OK) {
      FX_VLOGF(1, "proxy-rpc", "RPC callback failed, IO state %p, device %p: %s", conn.get(),
               conn->dev.get(), zx_status_get_string(r));
      return handle_destroy();
    }
    BeginWait(std::move(conn), dispatcher);
    return;
  }
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    FX_VLOGF(1, "proxy-rpc", "Peer closed, IO state %p, device %p", conn.get(), conn->dev.get());
    return handle_destroy();
  }
  LOGF(WARNING, "Unexpected signal state %#08x for device %p '%s'", signal->observed,
       conn->dev.get(), conn->dev->name);
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
