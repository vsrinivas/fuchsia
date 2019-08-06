// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/socket_connection.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

// Socket Server -----------------------------------------------------------------------------------

bool SocketServer::Init(uint16_t port) {
  server_socket_.reset(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  if (!server_socket_.is_valid()) {
    FXL_LOG(ERROR) << "Could not create socket.";
    return false;
  }

  // Bind to local address.
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons(port);
  if (bind(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    FXL_LOG(ERROR) << "Could not bind socket.";
    return false;
  }

  if (listen(server_socket_.get(), 1) < 0)
    return false;

  return true;
}

void SocketServer::Run(ConnectionConfig config) {
  // Wait for one connection.
  printf("Waiting on port %d for zxdb connection...\n", config.port);
  fflush(stdout);
  connection_ = std::make_unique<SocketConnection>(config.debug_agent);
  if (!connection_->Accept(config.message_loop, server_socket_.get()))
    return;

  printf("Connection established.\n");
}

void SocketServer::Reset() { connection_.reset(); }

// SocketConnection --------------------------------------------------------------------------------

SocketConnection::SocketConnection(debug_agent::DebugAgent* agent) : debug_agent_(agent) {}

SocketConnection::~SocketConnection() {
  if (!connected_)
    return;
  FXL_DCHECK(debug_agent_) << "A debug agent should be set when resetting the connection.";
  debug_agent_->Disconnect();
}

bool SocketConnection::Accept(debug_ipc::MessageLoop* main_thread_loop, int server_fd) {
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));

  socklen_t addrlen = sizeof(addr);
  fbl::unique_fd client(accept(server_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen));
  if (!client.is_valid()) {
    FXL_LOG(ERROR) << "Couldn't accept connection.";
    return false;
  }

  if (fcntl(client.get(), F_SETFL, O_NONBLOCK) < 0) {
    FXL_LOG(ERROR) << "Couldn't make port nonblocking.";
    return false;
  }

  // We need to post the agent initialization to the other thread.
  main_thread_loop->PostTask(
      FROM_HERE, [this, debug_agent = debug_agent_, client = std::move(client)]() mutable {
        if (!buffer_.Init(std::move(client))) {
          FXL_LOG(ERROR) << "Error waiting for data.";
          debug_ipc::MessageLoop::Current()->QuitNow();
          return;
        }

        // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
        adapter_ = std::make_unique<debug_agent::RemoteAPIAdapter>(debug_agent, &buffer_.stream());

        buffer_.set_data_available_callback(
            [adapter = adapter_.get()]() { adapter->OnStreamReadable(); });

        // Exit the message loop on error.
        buffer_.set_error_callback([]() {
          DEBUG_LOG(Agent) << "Connection lost.";
          debug_ipc::MessageLoop::Current()->QuitNow();
        });

        // Connect the buffer into the agent.
        debug_agent->Connect(&buffer_.stream());
      });

  printf("Accepted connection.\n");
  connected_ = true;
  return true;
}

}  // namespace debug_agent
