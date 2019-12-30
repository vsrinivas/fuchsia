// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/fdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

#include <optional>

#include "src/lib/fxl/command_line.h"
#include "src/ui/tools/print-input-report/devices.h"
#include "src/ui/tools/print-input-report/printer.h"

namespace print_input_report {

void PrintHelp(Printer* printer) {
  printer->Print("usage: print-input-report <command> [<args>]\n\n");
  printer->Print("  commands:\n");
  printer->Print("    read [<devpath> [num reads]]\n");
  printer->Print("    descriptor [<devpath>]\n");
}

zx_status_t ParseUintArg(const char* arg, uint32_t min, uint32_t max, uint32_t* out_val) {
  if ((arg == nullptr) || (out_val == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool is_hex = (arg[0] == '0') && (arg[1] == 'x');
  if (sscanf(arg, is_hex ? "%x" : "%u", out_val) != 1) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((*out_val < min) || (*out_val > max)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

std::optional<fuchsia_input_report::InputDevice::SyncClient> GetClientFromPath(
    Printer* printer, const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    printer->Print("could not open %s\n", path.c_str());
    return std::nullopt;
  }

  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
  if (status != ZX_OK) {
    printer->Print("Ftdio get handle failed with %s\n", zx_status_get_string(status));
    return std::nullopt;
  }

  return fuchsia_input_report::InputDevice::SyncClient(std::move(chan));
}

}  // namespace print_input_report

int main(int argc, const char** argv) {
  print_input_report::Printer printer;
  const fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto args = command_line.positional_args();
  if (args.size() == 0) {
    print_input_report::PrintHelp(&printer);
    return 0;
  }

  // The "read" command.
  if (args[0] == "read") {
    uint32_t num_reads = UINT32_MAX;
    // Parse "device_path.
    if (args.size() < 2) {
      PrintHelp(&printer);
      return 1;
    }

    // Parse "num_reads".
    if (args.size() > 2) {
      zx_status_t res =
          print_input_report::ParseUintArg(args[2].c_str(), 0, UINT32_MAX, &num_reads);
      if (res != ZX_OK) {
        printer.Print("Failed to parse <num reads> (res %s)\n", zx_status_get_string(res));
        PrintHelp(&printer);
        return 1;
      }
    }

    const std::string& device_path = args[1].c_str();
    auto sync_client = print_input_report::GetClientFromPath(&printer, device_path);
    if (!sync_client) {
      return -1;
    }

    printer.Print("Reading reports from %s:\n", device_path.c_str());
    return print_input_report::PrintInputReport(&printer, &sync_client.value(), num_reads);

    // The "descriptor" command.
  } else if (args[0] == "descriptor") {
    // Parse "device_path.
    if (args.size() < 2) {
      PrintHelp(&printer);
      return 1;
    }

    const std::string& device_path = args[1].c_str();
    auto sync_client = print_input_report::GetClientFromPath(&printer, device_path);
    if (!sync_client) {
      return -1;
    }
    return print_input_report::PrintInputDescriptor(&printer, &sync_client.value());
  };

  print_input_report::PrintHelp(&printer);
  return 0;
}
