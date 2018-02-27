// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/main_loop_zircon.h"

#include <fdio/io.h>
#include <unistd.h>
#include <zircon/syscalls/port.h>
#include <zx/event.h>

#include "garnet/bin/zxdb/client/agent_connection.h"
#include "lib/fxl/logging.h"

namespace zxdb {

namespace {

// Key used for watching stdio. This is not a valid connection_id assigned by
// MainLoop.
constexpr uint64_t kStdinKey = 0;

void RegisterFdioReadWithPort(fdio_t* fdio, zx::port* port) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_signals_t signals = ZX_SIGNAL_NONE;
  __fdio_wait_begin(fdio, POLLIN, &handle, &signals);
  if (handle != ZX_HANDLE_INVALID) {
    zx_object_wait_async(handle, port->get(), kStdinKey, signals,
                         ZX_WAIT_ASYNC_REPEATING);
  }
}

}  // namespace

PlatformMainLoop::PlatformMainLoop() : MainLoop() {
  zx::port::create(0, &port_);

  stdin_fdio_ = __fdio_fd_to_io(STDIN_FILENO);
  if (stdin_fdio_)
    RegisterFdioReadWithPort(stdin_fdio_, &port_);
}

PlatformMainLoop::~PlatformMainLoop() {}

void PlatformMainLoop::PlatformRun() {
  zx_port_packet_t packet;
  while (!should_quit() &&
         port_.wait(zx::time::infinite(), &packet, 0) == ZX_OK) {
    if (packet.key == kStdinKey) {
      // Event about stdin. __fdio_wait_end will convert the underlying-
      // handle-specific signal to a generic "read" one.
      uint32_t events = 0;
      __fdio_wait_end(stdin_fdio_, packet.signal.observed, &events);
      if (events & POLLIN)
        OnStdinReadable();
    } else {
      // Everything else should be a connection ID.
      AgentConnection* connection = ConnectionFromID(packet.key);
      FXL_DCHECK(connection);
      if (packet.signal.observed & ZX_SOCKET_READABLE)
        connection->OnNativeHandleReadable();
      if (packet.signal.observed & ZX_SOCKET_WRITABLE)
        connection->OnNativeHandleWritable();
      if (packet.signal.observed & ZX_SOCKET_PEER_CLOSED)
        set_should_quit();
    }
  }
}

void PlatformMainLoop::PlatformStartWatchingConnection(
    size_t connection_id, AgentConnection* connection) {
  zx_object_wait_async(connection->native_handle(), port_.get(), connection_id,
                       ZX_SOCKET_READABLE | ZX_SOCKET_WRITABLE |
                       ZX_SOCKET_PEER_CLOSED,
                       ZX_WAIT_ASYNC_REPEATING);
}

void PlatformMainLoop::PlatformStopWatchingConnection(
    size_t connection_id, AgentConnection* connection) {
  port_.cancel(connection->native_handle(), connection_id);
}

}  // namespace zxdb
