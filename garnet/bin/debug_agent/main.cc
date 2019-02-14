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
#include "garnet/lib/debug_ipc/debug/debug.h"
#include "garnet/lib/debug_ipc/helper/message_loop_async.h"
#include "garnet/lib/debug_ipc/helper/message_loop_target.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/sys/cpp/service_directory.h"

using namespace debug_ipc;

namespace debug_agent {
namespace {

std::unique_ptr<debug_ipc::MessageLoop> GetMessageLoop(
    MessageLoopTarget::LoopType type) {
  switch (type) {
    case MessageLoopTarget::LoopType::kAsync:
      return std::make_unique<debug_ipc::MessageLoopAsync>();
    case MessageLoopTarget::LoopType::kZircon:
      return std::make_unique<debug_ipc::MessageLoopZircon>();
    case MessageLoopTarget::LoopType::kLast:
      break;
  }

  FXL_NOTREACHED();
  return nullptr;
}

// SocketConnection ------------------------------------------------------------

// Represents one connection to a client.
class SocketConnection {
 public:
  SocketConnection(std::shared_ptr<sys::ServiceDirectory> services)
      : services_(services) {}
  ~SocketConnection() {}

  bool Accept(int server_fd);

  const debug_agent::DebugAgent* agent() const { return agent_.get(); }

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
  debug_ipc::BufferedFD buffer_;

  std::unique_ptr<debug_agent::DebugAgent> agent_;
  std::unique_ptr<debug_agent::RemoteAPIAdapter> adapter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketConnection);
};

bool SocketConnection::Accept(int server_fd) {
  sockaddr_in6 addr;
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

  printf("Accepted connection.\n");
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
           std::shared_ptr<sys::ServiceDirectory> services);

 private:
  bool AcceptNextConnection();

  fxl::UniqueFD server_socket_;
  std::unique_ptr<SocketConnection> connection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketServer);
};

bool SocketServer::Run(debug_ipc::MessageLoop* message_loop, int port,
                       std::shared_ptr<sys::ServiceDirectory> services) {
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
    printf("Waiting on port %d for zxdb connection...\n", port);
    connection_ = std::make_unique<SocketConnection>(services);
    if (!connection_->Accept(server_socket_.get()))
      return false;

    printf("Connection established.\n");

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
      [Experimental] Use the unwinder from AOSP.

  --async-message-loop
      [Experimental] Use async-loop backend message loop.

  --debug-message-loop
      Run the debug agent's message loop in debug mode.
      This prints all the enqueued tasks to the message loop.

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

  // By default use the original agent message loop.
  auto message_loop_type = MessageLoopTarget::LoopType::kZircon;
  if (cmdline.HasOption("async-message-loop")) {
    // Use new async loop.
    message_loop_type = MessageLoopTarget::LoopType::kAsync;
  }

  if (cmdline.HasOption("debug-message-loop")) {
    printf("Running message loop in debug mode.\n");
    debug_ipc::SetDebugMode(true);
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

    auto services = sys::ServiceDirectory::CreateFromNamespace();

    printf("Using %s message loop.\n",
           MessageLoopTarget::LoopTypeToString(message_loop_type));
    auto message_loop = debug_agent::GetMessageLoop(message_loop_type);
    message_loop->Init();

    // The scope ensures the objects are destroyed before calling Cleanup on the
    // MessageLoop.
    {
      debug_agent::SocketServer server;
      if (!server.Run(message_loop.get(), port, services))
        return 1;
    }
    message_loop->Cleanup();
  } else {
    fprintf(stderr, "ERROR: Port number required.\n\n");
    debug_agent::PrintUsage();
    return 1;
  }

  return 0;
}
