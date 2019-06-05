// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <cmdline/args_parser.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <istream>
#include <memory>

#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/remote_api_adapter.h"
#include "src/developer/debug/debug_agent/unwind.h"
#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"

using namespace debug_ipc;

namespace debug_agent {
namespace {

// Valid options for the --unwind flag.
const char kAospUnwinder[] = "aosp";
const char kNgUnwinder[] = "ng";

struct CommandLineOptions {
  int port = 0;
  bool debug_mode = false;
  std::string unwind = kAospUnwinder;
};

const char kHelpIntro[] = R"(debug_agent --port=<port> [ <options> ]

  The debug_agent provides the on-device stub for the ZXDB frontend to talk
  to. Once you launch the debug_agent, connect zxdb to the same port you
  provide on the command-line.

Options

)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints all command-line switches.)";

const char kPortHelp[] = R"(  --port=<port>
    [Required] TCP port number to listen to incoming connections on.)";

const char kDebugModeHelp[] = R"(  --debug-mode
  -d
      Run the agent on debug mode. This will enable conditional logging
      messages and timing profiling. Mainly useful for people developing zxdb.)";

const char kUnwindHelp[] = R"(  --unwind=[aosp|ng]
      Force using either the AOSP or NG unwinder for generating stack traces.)";

cmdline::Status ParseCommandLine(int argc, const char* argv[],
                                 CommandLineOptions* options) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("port", 0, kPortHelp, &CommandLineOptions::port);
  parser.AddSwitch("debug-mode", 'd', kDebugModeHelp,
                   &CommandLineOptions::debug_mode);
  parser.AddSwitch("unwind", 0, kUnwindHelp, &CommandLineOptions::unwind);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp,
                          [&requested_help]() { requested_help = true; });

  std::vector<std::string> params;
  cmdline::Status status = parser.Parse(argc, argv, options, &params);
  if (status.has_error())
    return status;

  // Handle --help switch since we're the one that knows about the switches.
  if (requested_help)
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());

  return cmdline::Status::Ok();
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

    DEBUG_LOG(Agent) << "Connection lost.";
    if (connection_->agent()->should_quit())
      break;
  }

  return true;
}

}  // namespace
}  // namespace debug_agent

// main ------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
  debug_agent::CommandLineOptions options;
  cmdline::Status status = ParseCommandLine(argc, argv, &options);
  if (status.has_error()) {
    fprintf(stderr, "%s\n", status.error_message().c_str());
    return 1;
  }

  // Decode the unwinder type.
  if (options.unwind == debug_agent::kAospUnwinder) {
    debug_agent::SetUnwinderType(debug_agent::UnwinderType::kAndroid);
  } else if (options.unwind == debug_agent::kNgUnwinder) {
    debug_agent::SetUnwinderType(debug_agent::UnwinderType::kNgUnwind);
  } else {
    fprintf(stderr, "Invalid option for --unwind. See debug_agent --help.\n");
    return 1;
  }

  // TODO(donosoc): Do correct category setup.
  debug_ipc::SetLogCategories({LogCategory::kAll});
  if (options.debug_mode) {
    printf("Running the debug agent in debug mode.\n");
    debug_ipc::SetDebugMode(true);
  }

  if (options.port) {
    auto services = sys::ServiceDirectory::CreateFromNamespace();

    auto message_loop = std::make_unique<MessageLoopTarget>();
    zx_status_t status = message_loop->InitTarget();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not initialize message loop: "
                     << debug_ipc::ZxStatusToString(status);
    }

    // The scope ensures the objects are destroyed before calling Cleanup on the
    // MessageLoop.
    {
      debug_agent::SocketServer server;
      if (!server.Run(message_loop.get(), options.port, services)) {
        message_loop->Cleanup();
        return 1;
      }
    }
    message_loop->Cleanup();
  } else {
    fprintf(
        stderr,
        "ERROR: --port=<port-number> required. See debug_agent --help.\n\n");
    return 1;
  }

  // It's very useful to have a simple message that informs the debug agent
  // exited successfully.
  fprintf(stderr, "\rSee you, Space Cowboy...\r\n");
  return 0;
}
