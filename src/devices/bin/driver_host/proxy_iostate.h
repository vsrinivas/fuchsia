// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_PROXY_IOSTATE_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_PROXY_IOSTATE_H_

#include <lib/async/cpp/wait.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>

#include <memory>

#include <fbl/ref_ptr.h>

#include "async_loop_owned_rpc_handler.h"

struct zx_device;

struct ProxyIostate : AsyncLoopOwnedRpcHandler<ProxyIostate> {
  explicit ProxyIostate(fbl::RefPtr<zx_device> device) : dev(std::move(device)) {}
  ~ProxyIostate();

  // Creates a ProxyIostate and points |dev| at it.  The ProxyIostate is owned
  // by the async loop, and its destruction may be requested by calling
  // Cancel().
  static zx_status_t Create(const fbl::RefPtr<zx_device>& dev, zx::channel rpc,
                            async_dispatcher_t* dispatcher);

  // Request the destruction of the proxy connection
  // The device for which ProxyIostate is currently attached to should have
  // its proxy_ios_lock held across CancelLocked().
  // We must disable thread safety analysis because the lock is in |this->dev|,
  // and Clang cannot reason about the aliasing involved.
  void CancelLocked(async_dispatcher_t* dispatcher) TA_NO_THREAD_SAFETY_ANALYSIS;

  static void HandleRpc(std::unique_ptr<ProxyIostate> conn, async_dispatcher_t* dispatcher,
                        async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

  const fbl::RefPtr<zx_device> dev;
};

namespace internal {
void proxy_ios_destroy(const fbl::RefPtr<zx_device>& dev);
}

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_PROXY_IOSTATE_H_
