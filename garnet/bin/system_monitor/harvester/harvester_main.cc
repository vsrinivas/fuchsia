// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>

#include <string>

#include "dockyard_proxy.h"
#include "dockyard_proxy_grpc.h"
#include "dockyard_proxy_local.h"
#include "harvester.h"
#include "root_resource.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

int main(int argc, char** argv) {
  constexpr int EXIT_CODE_OK = 0;
  // A broad 'something went wrong' error.
  constexpr int EXIT_CODE_GENERAL_ERROR = 1;

  // The wip number is incremented arbitrarily.
  // TODO(smbug.com/44) replace wip number with real version number.
  constexpr char VERSION_OUTPUT[] = "System Monitor Harvester - wip 11";

  // Command line options.
  constexpr char COMMAND_LOCAL[] = "local";
  constexpr char COMMAND_VERSION[] = "version";

  bool use_grpc = true;

  // Parse command line.
  FXL_LOG(INFO) << VERSION_OUTPUT;
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }
  if (command_line.HasOption(COMMAND_VERSION)) {
    std::cout << VERSION_OUTPUT << std::endl;
    exit(EXIT_CODE_OK);
  }
  if (command_line.HasOption(COMMAND_LOCAL)) {
    FXL_LOG(INFO) << "Option: local only, not using transport to Dockyard.";
    use_grpc = false;
  }

  // Set up.
  std::unique_ptr<harvester::DockyardProxy> dockyard_proxy;
  if (use_grpc) {
    const auto& positional_args = command_line.positional_args();
    if (positional_args.size() < 1) {
      // TODO(smbug.com/30): Adhere to CLI tool requirements for --help.
      std::cerr << "Please specify an IP:Port, such as localhost:50051"
                << std::endl;
      exit(EXIT_CODE_GENERAL_ERROR);
    }

    // TODO(smbug.com/32): This channel isn't authenticated
    // (InsecureChannelCredentials()).
    dockyard_proxy =
        std::make_unique<harvester::DockyardProxyGrpc>(grpc::CreateChannel(
            positional_args[0], grpc::InsecureChannelCredentials()));

    if (!dockyard_proxy ||
        dockyard_proxy->Init() != harvester::DockyardProxyStatus::OK) {
      exit(EXIT_CODE_GENERAL_ERROR);
    }
  } else {
    dockyard_proxy = std::make_unique<harvester::DockyardProxyLocal>();
  }

  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  if (ret != ZX_OK) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  harvester::Harvester harvester(root_resource, loop.dispatcher(),
                                 std::move(dockyard_proxy));
  harvester.GatherData();
  loop.Run();

  FXL_LOG(INFO) << "System Monitor Harvester - exiting";
  return 0;
}
