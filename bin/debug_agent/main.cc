// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <memory>

#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/bin/debug_agent/remote_api_adapter.h"
#include "garnet/lib/debug_ipc/helper/buffered_fd.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"

namespace debug_agent {
namespace {

// SocketConnection ------------------------------------------------------------

// Represents one connection to a client.
class SocketConnection {
 public:
  SocketConnection() {}
  ~SocketConnection() {}

  bool Accept(int server_fd);

 private:
  debug_ipc::BufferedFD buffer_;

  std::unique_ptr<debug_agent::DebugAgent> agent_;
  std::unique_ptr<debug_agent::RemoteAPIAdapter> adapter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketConnection);
};

bool SocketConnection::Accept(int server_fd) {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));

  socklen_t addrlen = sizeof(addr);
  fxl::UniqueFD client(
      accept(server_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen));
  if (!client.is_valid()) {
    fprintf(stderr, "Couldn't accept connection.\n");
    return false;
  }
  if (fcntl(client.get(), F_SETFL, O_NONBLOCK) < 0) {
    fprintf(stderr, "Couldn't make port nonblocking.\n");
    return false;
  }

  if (!buffer_.Init(std::move(client))) {
    fprintf(stderr, "Error waiting for data.\n");
    return false;
  }

  // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
  agent_ = std::make_unique<debug_agent::DebugAgent>(&buffer_.stream());
  adapter_ = std::make_unique<debug_agent::RemoteAPIAdapter>(agent_.get(),
                                                             &buffer_.stream());
  buffer_.set_data_available_callback(
      [adapter = adapter_.get()]() { adapter->OnStreamReadable(); });

  // Exit the message loop on error.
  buffer_.set_error_callback(
      []() { debug_ipc::MessageLoop::Current()->QuitNow(); });

  printf("Accepted connection.\n");
  return true;
}

// SocketServer ----------------------------------------------------------------

// Listens for connections on a socket. Only one connection is supported at a
// time. It waits for connections in a blocking fashion, and then runs the
// message loop on that connection.
class SocketServer {
 public:
  SocketServer();
  ~SocketServer();

  bool Run(int port);

 private:
  bool AcceptNextConnection();

  fxl::UniqueFD server_socket_;
  std::unique_ptr<SocketConnection> connection_;

  debug_ipc::MessageLoopZircon message_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketServer);
};

SocketServer::SocketServer() { message_loop_.Init(); }

SocketServer::~SocketServer() { message_loop_.Cleanup(); }

bool SocketServer::Run(int port) {
  server_socket_.reset(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  if (!server_socket_.is_valid()) {
    fprintf(stderr, "Could not create socket.\n");
    return false;
  }

  // Bind to local address.
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons(port);
  if (bind(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr),
           sizeof(addr)) < 0) {
    fprintf(stderr, "Could not bind socket.\n");
    return false;
  }

  if (listen(server_socket_.get(), 1) < 0)
    return false;

  while (true) {
    // Wait for one connection.
    printf("Waiting on port %d for zxdb connection...\n", port);
    connection_ = std::make_unique<SocketConnection>();
    if (!connection_->Accept(server_socket_.get()))
      return false;

    printf("Connection established.\n");

    // Run the debug agent for this connection.
    message_loop_.Run();

    printf("Connection closed.\n");
  }

  return true;
}

void PrintUsage() {
  const char kUsage[] = R"(Usage

  debug_agent --port=<port>

Arguments

  --port (required)
    TCP port number to listen to incoming connections on.
)";

  fprintf(stderr, "%s", kUsage);
}

}  // namespace
}  // namespace debug_agent

// main ------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  fxl::CommandLine cmdline = fxl::CommandLineFromArgcArgv(argc, argv);
  if (cmdline.HasOption("help")) {
    debug_agent::PrintUsage();
    return 0;
  }

  std::string value;
  if (cmdline.GetOptionValue("port", &value)) {
    // TCP port listen mode.
    char* endptr = nullptr;
    int port = strtol(value.c_str(), &endptr, 10);
    if (value.empty() || endptr != &value.c_str()[value.size()]) {
      fprintf(stderr, "ERROR: Port number not a valid number.\n");
      return 1;
    }

    debug_agent::SocketServer server;
    if (!server.Run(port))
      return 1;
  } else {
    fprintf(stderr, "ERROR: Port number required.\n\n");
    debug_agent::PrintUsage();
    return 1;
  }

  return 0;
}
