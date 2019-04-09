// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <grpc++/grpc++.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "dockyard_proxy.h"
#include "dockyard_proxy_grpc.h"
#include "dockyard_proxy_local.h"
#include "harvester.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace {

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  const char* sysinfo = "/dev/misc/sysinfo";
  int fd = open(sysinfo, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Cannot open sysinfo: " << strerror(errno);
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain sysinfo channel: "
                   << zx_status_get_string(status);
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, root_resource);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(fidl_status);
    return fidl_status;
  } else if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

}  // namespace

int main(int argc, char** argv) {
  constexpr int EXIT_CODE_OK = 0;
  // A broad 'something went wrong' error.
  constexpr int EXIT_CODE_GENERAL_ERROR = 1;

  // The wip number is incremented arbitrarily.
  // TODO(dschuyler) replace wip number with real version number.
  constexpr char VERSION_OUTPUT[] = "System Monitor Harvester - wip 10";

  // Command line options.
  constexpr char COMMAND_INSPECT[] = "inspect";
  constexpr char COMMAND_UPDATE_PERIOD[] = "msec-update-period";
  constexpr char COMMAND_LOCAL[] = "local";
  constexpr char COMMAND_VERSION[] = "version";

  bool use_grpc = true;
  int cycle_msec_period = 100;

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
  if (command_line.HasOption(COMMAND_INSPECT)) {
    FXL_LOG(INFO) << "Enabled component framework inspection (wip)";
    // TODO(dschuyler): actually do component framework inspection.
  }
  if (command_line.HasOption(COMMAND_UPDATE_PERIOD)) {
    std::string update_period;
    command_line.GetOptionValue(COMMAND_UPDATE_PERIOD, &update_period);
    if (!fxl::StringToNumberWithError(update_period, &cycle_msec_period)) {
      std::cerr << "Error: Unable to parse `--" << COMMAND_UPDATE_PERIOD
                << "` value of \"" << update_period << "\"" << std::endl;
      exit(EXIT_CODE_GENERAL_ERROR);
    }
  }
  FXL_LOG(INFO) << "Gathering data every " << cycle_msec_period << " msec.";

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
  zx_status_t ret = get_root_resource(&root_resource);
  if (ret != ZX_OK) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  harvester::Harvester harvester(zx::msec(cycle_msec_period), root_resource,
                                 loop.dispatcher(), dockyard_proxy.release());
  harvester.GatherData();
  loop.Run();

  FXL_LOG(INFO) << "System Monitor Harvester - exiting";
  return 0;
}
