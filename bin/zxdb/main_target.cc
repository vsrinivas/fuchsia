// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>

#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/bin/debug_agent/exception_handler.h"
#include "garnet/bin/zxdb/client/agent_connection.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/main_loop_zircon.h"

// Main function for the debugger run on Zircon. This currently runs the
// debug_agent in-process to avoid IPC.
int main(int argc, char* argv[]) {
  // Create a socket to talk to the ExceptionHandler. Talking sockets to
  // ourselves keeps the same codepath regardless of whether the debug_agent
  // code is running in process or remotely.
  zx::socket client_socket, router_socket;
  if (zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &router_socket) !=
      ZX_OK) {
    fprintf(stderr, "Can't create socket, aborting.\n");
    return 1;
  }

  // Start the exception handler and DebugAgent class on the background thread.
  ExceptionHandler handler;
  DebugAgent agent(&handler);
  handler.set_sink(&agent);
  if (!handler.Start(std::move(router_socket))) {
    fprintf(stderr, "Can't start thread, aborting.\n");
    return 1;
  }

  zxdb::Session session;
  session.SetAgentConnection(std::make_unique<zxdb::AgentConnection>(
      &session, client_socket.release()));

  zxdb::Console console(&session);
  console.Init();

  zxdb::PlatformMainLoop main_loop;

  // TODO(brettw) have a better way to hook this up. Probably the main loop
  // needs to be more generic and live in client/. Then there can be a global
  // and the AgentConnection can register and unregister itself.
  main_loop.StartWatchingConnection(session.agent_connection());
  main_loop.Run();
  main_loop.StopWatchingConnection(session.agent_connection());

  // Ensure the ExceptionHandler doesn't try to use the sink after it's
  // destroyed (the sink will be destroyed first).
  handler.Shutdown();
  handler.set_sink(nullptr);
  return 0;
}
