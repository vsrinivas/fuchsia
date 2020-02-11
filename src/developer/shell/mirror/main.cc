// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <thread>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/shell/mirror/command_line_options.h"
#include "src/developer/shell/mirror/server.h"

namespace shell::mirror {

// Main function for the server.
int ConsoleMain(int argc, const char** argv) {
  CommandLineOptions options;
  std::vector<std::string> params;
  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params);
  if (status.has_error()) {
    FXL_LOG(ERROR) << status.error_message();
    return 1;
  }

  SocketServer::ConnectionConfig config;
  config.port = options.port;
  config.path = options.path;

  SocketServer server;
  server.RunInLoop(config, FROM_HERE, []() {});

  return 0;
}

}  // namespace shell::mirror

int main(int argc, const char** argv) { return shell::mirror::ConsoleMain(argc, argv); }
