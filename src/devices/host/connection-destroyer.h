// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_CONNECTION_DESTROYER_H_
#define SRC_DEVICES_HOST_CONNECTION_DESTROYER_H_

#include <lib/async/cpp/receiver.h>
#include <zircon/syscalls.h>

namespace devmgr {

class DeviceControllerConnection;
struct ProxyIostate;

// Handles destroying Connection objects in the single-threaded DevhostAsyncLoop().
// This allows us to prevent races between canceling waiting on the connection
// channel and executing the connection's handler.
class ConnectionDestroyer {
 public:
  static ConnectionDestroyer* Get() {
    static ConnectionDestroyer destroyer;
    return &destroyer;
  }

  zx_status_t QueueDeviceControllerConnection(async_dispatcher_t* dispatcher,
                                              DeviceControllerConnection* conn);
  zx_status_t QueueProxyConnection(async_dispatcher_t* dispatcher, ProxyIostate* conn);

 private:
  ConnectionDestroyer() = default;

  ConnectionDestroyer(const ConnectionDestroyer&) = delete;
  ConnectionDestroyer& operator=(const ConnectionDestroyer&) = delete;

  ConnectionDestroyer(ConnectionDestroyer&&) = delete;
  ConnectionDestroyer& operator=(ConnectionDestroyer&&) = delete;

  static void Handler(async_dispatcher_t* dispatcher, async::Receiver* receiver, zx_status_t status,
                      const zx_packet_user_t* data);

  enum class Type {
    DeviceController,
    Proxy,
  };

  async::Receiver receiver_{ConnectionDestroyer::Handler};
};

}  // namespace devmgr

#endif  // SRC_DEVICES_HOST_CONNECTION_DESTROYER_H_
