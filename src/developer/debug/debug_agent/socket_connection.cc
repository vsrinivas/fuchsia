// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/socket_connection.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

// Socket Server -----------------------------------------------------------------------------------

const DebugAgent* SocketServer::GetDebugAgent() const {
  if (!connected())
    return nullptr;
  return connection_->agent();
}

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

void SocketServer::Run(debug_ipc::MessageLoop* main_thread_loop, int port,
                       std::shared_ptr<sys::ServiceDirectory> services) {
  // Wait for one connection.
  printf("Waiting on port %d for zxdb connection...\n", port);
  fflush(stdout);
  connection_ = std::make_unique<SocketConnection>(services);
  if (!connection_->Accept(main_thread_loop, server_socket_.get()))
    return;

  printf("Connection established.\n");
}

void SocketServer::Reset() { connection_.reset(); }

// SocketConnection --------------------------------------------------------------------------------

bool SocketConnection::Accept(debug_ipc::MessageLoop* main_thread_loop, int server_fd) {
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));

  socklen_t addrlen = sizeof(addr);
  fxl::UniqueFD client(accept(server_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen));
  if (!client.is_valid()) {
    FXL_LOG(ERROR) << "Couldn't accept connection.";
    return false;
  }

  if (fcntl(client.get(), F_SETFL, O_NONBLOCK) < 0) {
    FXL_LOG(ERROR) << "Couldn't make port nonblocking.";
    return false;
  }

  // We need to post the agent initialization to the other thread.
  main_thread_loop->PostTask(FROM_HERE, [this, client = std::move(client)]() mutable {
    if (!buffer_.Init(std::move(client))) {
      FXL_LOG(ERROR) << "Error waiting for data.";
      debug_ipc::MessageLoop::Current()->QuitNow();
      return;
    }

    // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
    agent_ = std::make_unique<debug_agent::DebugAgent>(&buffer_.stream(), services_);
    adapter_ = std::make_unique<debug_agent::RemoteAPIAdapter>(agent_.get(), &buffer_.stream());

    buffer_.set_data_available_callback(
        [adapter = adapter_.get()]() { adapter->OnStreamReadable(); });

    // Exit the message loop on error.
    buffer_.set_error_callback([]() {
      DEBUG_LOG(Agent) << "Connection lost.";
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  });

  printf("Accepted connection.\n");
  return true;
}

}  // namespace debug_agent
