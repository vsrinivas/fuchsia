// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <fuchsia/memory/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <grpc++/grpc++.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>

#include "harvester.h"
#include "harvester_grpc.h"
#include "harvester_local.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"

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
  constexpr char VERSION_OUTPUT[] = "System Monitor Harvester - wip 9";

  // Command line options.
  constexpr char COMMAND_INSPECT[] = "inspect";
  constexpr char COMMAND_RATE[] = "msec-rate";
  constexpr char COMMAND_LOCAL[] = "local";
  constexpr char COMMAND_VERSION[] = "version";

  bool use_grpc = true;
  int cycle_msec_rate = 100;

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
  if (command_line.HasOption(COMMAND_RATE)) {
    std::string rate;
    command_line.GetOptionValue(COMMAND_RATE, &rate);
    if (fxl::StringToNumberWithError(rate, &cycle_msec_rate)) {
      FXL_LOG(INFO) << "Gathering data at a cycle rate of " << cycle_msec_rate;
    } else {
      std::cerr << "Error: Unable to parse `--rate` value." << std::endl;
      exit(EXIT_CODE_GENERAL_ERROR);
    }
  }

  // Set up.
  std::unique_ptr<harvester::Harvester> harvester;
  if (use_grpc) {
    const auto& positional_args = command_line.positional_args();
    if (positional_args.size() < 1) {
      // TODO(dschuyler): Adhere to CLI tool requirements for --help.
      std::cerr << "Please specify an IP:Port, such as localhost:50051"
                << std::endl;
      exit(EXIT_CODE_GENERAL_ERROR);
    }

    // TODO(dschuyler): This channel isn't authenticated
    // (InsecureChannelCredentials()).
    harvester = std::make_unique<harvester::HarvesterGrpc>(grpc::CreateChannel(
        positional_args[0], grpc::InsecureChannelCredentials()));

    if (!harvester || harvester->Init() != harvester::HarvesterStatus::OK) {
      exit(EXIT_CODE_GENERAL_ERROR);
    }
  } else {
    harvester = std::make_unique<harvester::HarvesterLocal>();
  }

  zx_handle_t root_resource;
  zx_status_t ret = get_root_resource(&root_resource);
  if (ret != ZX_OK) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }

  // The main loop.
  while (true) {
    harvester::GatherCpuSamples(root_resource, harvester);
    harvester::GatherMemorySamples(root_resource, harvester);
    harvester::GatherThreadSamples(root_resource, harvester);
    // TODO(dschuyler): make this actually run at rate (i.e. remove drift from
    // execution time).
    std::this_thread::sleep_for(std::chrono::milliseconds(cycle_msec_rate));
  }
  FXL_LOG(INFO) << "System Monitor Harvester - exiting";
  return 0;
}
