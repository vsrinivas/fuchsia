// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "src/developer/debug/zxdb/client/cloud_storage_symbol_server.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/lib/fxl/strings/trim.h"
#include "tools/symbolizer/command_line_options.h"
#include "tools/symbolizer/log_parser.h"
#include "tools/symbolizer/printer.h"
#include "tools/symbolizer/symbolizer_impl.h"

namespace symbolizer {

namespace {

// TODO(dangyi): This is a poor implementation of the authentication process. Revisit this after
// fxb/61746 is resolved.
int AuthMode() {
  debug_ipc::MessageLoopPoll loop;
  loop.Init(nullptr);

  auto server = zxdb::CloudStorageSymbolServer::Impl(nullptr, "");
  if (server->state() == zxdb::SymbolServer::State::kBusy) {
    server->set_state_change_callback(
        [&](zxdb::SymbolServer*, zxdb::SymbolServer::State state) { loop.QuitNow(); });
    loop.Run();
    // Clear the callback.
    server->set_state_change_callback({});
  }

  if (server->state() == zxdb::SymbolServer::State::kReady) {
    std::cout << "You have already authenticated. To use another credential, please remove "
              << "~/.fuchsia/debug/googleapi_auth.\n";
    return EXIT_SUCCESS;
  }

  std::string key;
  std::cout << "To authenticate, please supply an authentication token. "
            << "You can retrieve a token from:\n"
            << server->AuthInfo() << '\n'
            << "Enter the server authentication key: ";
  std::cin >> key;

  int exit_code;
  server->Authenticate(key, [&loop, &exit_code](const zxdb::Err& err) {
    if (err.has_error()) {
      std::cout << "Server authentication failed: " << err.msg() << ".\n";
      exit_code = EXIT_FAILURE;
    } else {
      std::cout << "Authentication successful.\n";
      exit_code = EXIT_SUCCESS;
    }
    loop.QuitNow();
  });

  loop.Run();
  loop.Cleanup();

  return exit_code;
}

}  // namespace

int Main(int argc, const char* argv[]) {
  CommandLineOptions options;

  if (const Error error = ParseCommandLine(argc, argv, &options); !error.empty()) {
    // Sometimes the error just has too many "\n" at the end.
    std::cerr << fxl::TrimString(error, "\n") << std::endl;
    return EXIT_FAILURE;
  }

  if (options.auth_mode) {
    return AuthMode();
  }

  Printer printer(std::cout);
  SymbolizerImpl symbolizer(&printer, options);
  LogParser parser(std::cin, &printer, &symbolizer);

  while (parser.ProcessOneLine()) {
    // until the eof in the input.
  }

  return EXIT_SUCCESS;
}

}  // namespace symbolizer

int main(int argc, const char* argv[]) { return symbolizer::Main(argc, argv); }
