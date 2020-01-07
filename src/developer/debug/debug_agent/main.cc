// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>
#include <lib/zx/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>
#include <thread>

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/socket_connection.h"
#include "src/developer/debug/debug_agent/unwind.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
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
  std::string unwind = kNgUnwinder;
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

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("port", 0, kPortHelp, &CommandLineOptions::port);
  parser.AddSwitch("debug-mode", 'd', kDebugModeHelp, &CommandLineOptions::debug_mode);
  parser.AddSwitch("unwind", 0, kUnwindHelp, &CommandLineOptions::unwind);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  std::vector<std::string> params;
  cmdline::Status status = parser.Parse(argc, argv, options, &params);
  if (status.has_error())
    return status;

  // Handle --help switch since we're the one that knows about the switches.
  if (requested_help)
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());

  return cmdline::Status::Ok();
}

void ExceptionWatcherFunction(zx::channel* exception_channel) {
  // Watch the exception channel.
  zx_status_t status = exception_channel->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), 0);
  if (status != ZX_OK && status != ZX_ERR_CANCELED) {
    printf("Stopped listening on main thread's exception channel: %s\n",
           zx_status_get_string(status));
    return;
  }

  // If the channel was canceled, it means the main loop exited.
  if (status == ZX_ERR_CANCELED)
    return;

  zx::exception exception;
  zx_exception_info_t info;
  status = exception_channel->read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                   nullptr, nullptr);
  if (status != ZX_OK) {
    printf("Could not read main thread's exception: %s\n", zx_status_get_string(status));
    return;
  }

  // Flush the debug log.
  printf("********** Main thread excepted! Flushing logs... *************\n");
  fflush(stdout);
  FlushLogEntries();

  // Resume the exception (will propagate the crash) and terminate the process.
  exception.reset();
  zx_process_exit(1);
}

std::unique_ptr<std::thread> CreateExceptionWatcher(zx::channel* out) {
  zx_status_t status = zx::thread::self()->create_exception_channel(0, out);
  if (status != ZX_OK) {
    printf("Could not bind to main thread's exception channel: %s\n", zx_status_get_string(status));
    return nullptr;
  }

  return std::make_unique<std::thread>(ExceptionWatcherFunction, out);
}

}  // namespace
}  // namespace debug_agent

// main --------------------------------------------------------------------------------------------

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

    zx::channel exception_channel;
    auto exception_watcher = debug_agent::CreateExceptionWatcher(&exception_channel);

    auto message_loop = std::make_unique<PlatformMessageLoop>();
    std::string init_error_message;
    if (!message_loop->Init(&init_error_message)) {
      FXL_LOG(ERROR) << init_error_message;
      return 1;
    }

    // The scope ensures the objects are destroyed before calling Cleanup on the MessageLoop.
    {
      // The debug agent is independent of whether it's connected or not.
      // DebugAgent::Disconnect is called by ~SocketConnection is called by ~SocketServer, so the
      // debug agent must be destructed after the SocketServer.
      debug_agent::DebugAgent debug_agent(services,
                                          debug_agent::SystemProviders::CreateDefaults(services));

      debug_agent::SocketServer server;
      if (!server.Init(options.port)) {
        message_loop->Cleanup();
        return 1;
      }

      // The following loop will attempt to patch a stream to the debug agent in order to enable
      // communication.
      while (true) {
        // Start a new thread that will listen on a socket from an incoming connection from a
        // client. In the meantime, the main thread will block waiting for something to be posted
        // to the main thread.
        //
        // When the connection thread effectively receives a connection, it will post a task to the
        // loop to create the agent and begin normal debugger operation. Once the application quits
        // the loop, the code will clean the connection thread and create another or exit the loop,
        // according to the current agent's configuration.
        {
          debug_agent::SocketServer::ConnectionConfig conn_config;
          conn_config.message_loop = message_loop.get();
          conn_config.debug_agent = &debug_agent;
          conn_config.port = options.port;
          std::thread conn_thread(&debug_agent::SocketServer::Run, &server, std::move(conn_config));

          message_loop->Run();

          DEBUG_LOG(Agent) << "Joining connection thread.";
          conn_thread.join();
        }

        // See if the debug agent was told to exit.
        if (debug_agent.should_quit())
          break;

        // Prepare for another connection.
        // The resources need to be freed on the message loop's thread.
        server.Reset();
      }
    }
    message_loop->Cleanup();

    // Clean up the exception watcher.
    if (exception_watcher) {
      exception_channel.reset();  // This will stop the thread from waiting on this channel.
      exception_watcher->join();
    }
  } else {
    fprintf(stderr, "ERROR: --port=<port-number> required. See debug_agent --help.\n\n");
    return 1;
  }

  // It's very useful to have a simple message that informs the debug agent
  // exited successfully.
  fprintf(stderr, "\rSee you, Space Cowboy...\r\n");
  return 0;
}
