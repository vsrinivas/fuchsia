// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <fdio/io.h>
#include <launchpad/launchpad.h>
#include <lib/zx/process.h>

#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/bin/debug_agent/remote_api_adapter.h"
#include "garnet/lib/debug_ipc/helper/buffered_zx_socket.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

// Currently this is just a manual test that sets up some infrastructure and
// runs the message loop.

int main(int argc, char* argv[]) {
  zx::socket client_socket, router_socket;
  if (zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &router_socket) !=
      ZX_OK)
    fprintf(stderr, "Can't create socket.\n");

  debug_ipc::MessageLoopZircon message_loop;
  message_loop.Init();

  debug_ipc::BufferedZxSocket router_buffer;
  if (!router_buffer.Init(std::move(router_socket))) {
    fprintf(stderr, "Can't hook up stream.");
    return 1;
  }

  // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
  debug_agent::DebugAgent agent(&router_buffer.stream());
  debug_agent::RemoteAPIAdapter adapter(&agent, &router_buffer.stream());
  router_buffer.set_data_available_callback(
    [&adapter](){ adapter.OnStreamReadable(); });

  message_loop.Run();
  message_loop.Cleanup();
  return 0;
}
