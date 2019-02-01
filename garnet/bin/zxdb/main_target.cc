// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <zx/socket.h>
#include <memory>
#include <thread>

#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/bin/debug_agent/remote_api_adapter.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/lib/debug_ipc/helper/buffered_zx_socket.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

namespace {

// Background thread function that runs the in-process debug agent. The loop
// must outlive this thread.
void AgentThread(debug_ipc::MessageLoopZircon* loop, zx::socket socket) {
  // Bind the message loop to this thread.
  loop->Init();

  // This scope forces all the objects to be destroyed before the Cleanup()
  // call which will mark the message loop as not-current.
  {
    debug_ipc::BufferedZxSocket router_buffer;
    if (!router_buffer.Init(std::move(socket))) {
      fprintf(stderr, "Can't hook up stream.");
      return;
    }

    // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
    debug_agent::DebugAgent agent(&router_buffer.stream());
    debug_agent::RemoteAPIAdapter adapter(&agent, &router_buffer.stream());
    router_buffer.set_data_available_callback(
        [&adapter]() { adapter.OnStreamReadable(); });

    loop->Run();
  }
  loop->Cleanup();
}

}  // namespace

// Main function for the debugger run on Zircon. This currently runs the
// debug_agent in-process to avoid IPC.
int main(int argc, char* argv[]) {
  // Create a socket to talk to the in-process debug agent. Talking sockets to
  // ourselves keeps the same codepath regardless of whether the debug_agent
  // code is running in process or remotely.
  zx::socket client_socket, agent_socket;
  if (zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &agent_socket) !=
      ZX_OK) {
    fprintf(stderr, "Can't create socket, aborting.\n");
    return 1;
  }

  // Start background thread to run the agent in-process.
  debug_ipc::MessageLoopZircon agent_loop;
  std::thread agent_thread(&AgentThread, &agent_loop, std::move(agent_socket));

  // Client message loop.
  debug_ipc::MessageLoopZircon client_loop;
  client_loop.Init();

  // This scope forces all the objects to be destroyed before the Cleanup()
  // call which will mark the message loop as not-current.
  {
    debug_ipc::BufferedZxSocket buffer;
    if (!buffer.Init(std::move(client_socket))) {
      fprintf(stderr, "Can't hook up stream.");
      return 1;
    }

    // Route data from buffer -> session.
    zxdb::Session session(&buffer.stream());
    buffer.set_data_available_callback(
        [&session]() { session.OnStreamReadable(); });

    zxdb::Console console(&session);
    console.Init();

    client_loop.Run();
  }
  client_loop.Cleanup();

  // Ask the background thread to stop and join.
  agent_loop.PostTask([]() { debug_ipc::MessageLoop::Current()->QuitNow(); });
  agent_thread.join();
  return 0;
}
