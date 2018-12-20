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
#include "garnet/bin/debug_agent/unwind.h"
#include "garnet/lib/debug_ipc/helper/buffered_fd.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/svc/cpp/services.h"

namespace debug_agent {
namespace {

// SocketConnection ------------------------------------------------------------

// Represents one connection to a client.
class SocketConnection {
 public:
  SocketConnection(std::shared_ptr<component::Services> services)
      : services_(services) {}
  ~SocketConnection() {}

  bool Accept(int server_fd);

  const debug_agent::DebugAgent* agent() const { return agent_.get(); }

 private:
  std::shared_ptr<component::Services> services_;
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
    FXL_LOG(ERROR) << "Couldn't accept connection.";
    return false;
  }
  if (fcntl(client.get(), F_SETFL, O_NONBLOCK) < 0) {
    FXL_LOG(ERROR) << "Couldn't make port nonblocking.";
    return false;
  }

  if (!buffer_.Init(std::move(client))) {
    FXL_LOG(ERROR) << "Error waiting for data.";
    return false;
  }

  // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
  agent_ =
      std::make_unique<debug_agent::DebugAgent>(&buffer_.stream(), services_);
  adapter_ = std::make_unique<debug_agent::RemoteAPIAdapter>(agent_.get(),
                                                             &buffer_.stream());
  buffer_.set_data_available_callback(
      [adapter = adapter_.get()]() { adapter->OnStreamReadable(); });

  // Exit the message loop on error.
  buffer_.set_error_callback(
      []() { debug_ipc::MessageLoop::Current()->QuitNow(); });

  FXL_LOG(INFO) << "Accepted connection.";
  return true;
}

// SocketServer ----------------------------------------------------------------

// Listens for connections on a socket. Only one connection is supported at a
// time. It waits for connections in a blocking fashion, and then runs the
// message loop on that connection.
class SocketServer {
 public:
  SocketServer() = default;
  bool Run(debug_ipc::MessageLoop*, int port,
           std::shared_ptr<component::Services> services);

 private:
  bool AcceptNextConnection();

  fxl::UniqueFD server_socket_;
  std::unique_ptr<SocketConnection> connection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketServer);
};

bool SocketServer::Run(debug_ipc::MessageLoop* message_loop, int port,
                       std::shared_ptr<component::Services> services) {
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
  if (bind(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr),
           sizeof(addr)) < 0) {
    FXL_LOG(ERROR) << "Could not bind socket.";
    return false;
  }

  if (listen(server_socket_.get(), 1) < 0)
    return false;

  while (true) {
    // Wait for one connection.
    FXL_LOG(INFO) << "Waiting on port " << port << " for zxdb connection...";
    connection_ = std::make_unique<SocketConnection>(services);
    if (!connection_->Accept(server_socket_.get()))
      return false;

    FXL_LOG(INFO) << "Connection established.";

    // Run the debug agent for this connection.
    message_loop->Run();

    if (connection_->agent()->should_quit())
      break;
  }

  return true;
}

void PrintUsage() {
  const char kUsage[] = R"(Usage

  debug_agent --port=<port>

Arguments

  --aunwind
      Use the experimental unwinder from AOSP.

  --help
      Print this help.

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

  if (cmdline.HasOption("aunwind")) {
    // Use the Android unwinder.
    printf("Using AOSP unwinder (experimental).\n");
    debug_agent::SetUnwinderType(debug_agent::UnwinderType::kAndroid);
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

    auto environment_services = component::GetEnvironmentServices();


    debug_ipc::MessageLoopZircon message_loop;
    message_loop.Init();

    // The scope ensures the objects are destroyed before calling Cleanup on the
    // MessageLoop.
    {
      debug_agent::SocketServer server;
      if (!server.Run(&message_loop, port, environment_services))
        return 1;
    }
    message_loop.Cleanup();
  } else {
    fprintf(stderr, "ERROR: Port number required.\n\n");
    debug_agent::PrintUsage();
    return 1;
  }

  return 0;
}
