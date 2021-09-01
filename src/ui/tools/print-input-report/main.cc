// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

#include <optional>

#include <fbl/unique_fd.h>

#include "src/lib/fsl/io/device_watcher.h"
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

std::optional<fidl::WireSharedClient<fuchsia_input_report::InputDevice>> GetClientFromPath(
    Printer* printer, const std::string& path, async_dispatcher_t* dispatcher) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    printer->Print("could not open %s\n", path.c_str());
    return std::nullopt;
  }

  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
  if (status != ZX_OK) {
    printer->Print("fdio_get_service_handle failed with %s\n", zx_status_get_string(status));
    return std::nullopt;
  }

  return fidl::WireSharedClient(fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(chan)),
                                dispatcher);
}

int ReadAllDevices(async::Loop* loop, Printer* printer) {
  // Start watching the directory and read all of the input reports for each.
  auto watcher = fsl::DeviceWatcher::Create(
      "/dev/class/input-report/", [&](int dir_fd, const std::string& filename) {
        printer->Print("Reading reports from %s:\n", filename.c_str());
        fbl::unique_fd fd(openat(dir_fd, filename.c_str(), O_RDWR));
        if (!fd.is_valid()) {
          printer->Print("could not open %s\n", filename.c_str());
          return;
        }

        zx::channel chan;
        zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
        if (status != ZX_OK) {
          printer->Print("fdio_get_service_handle failed with %s\n", zx_status_get_string(status));
          return;
        }

        auto device = fidl::WireSharedClient(
            fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(chan)),
            loop->dispatcher());

        auto res = print_input_report::GetReaderClient(&device, loop->dispatcher());
        if (!res.is_ok()) {
          printer->Print("Failed to GetReaderClient\n");
          return;
        }
        auto reader = fidl::WireSharedClient<fuchsia_input_report::InputReportsReader>(
            std::move(res.value()));
        print_input_report::PrintInputReports(filename, printer, std::move(reader), UINT32_MAX);
      });

  loop->Run();
  return 0;
}

int ReadAllDescriptors(async::Loop* loop, Printer* printer) {
  // Start watching the directory and read all of the input reports for each.
  auto watcher = fsl::DeviceWatcher::Create(
      "/dev/class/input-report/", [&](int dir_fd, const std::string& filename) {
        printer->Print("Reading descriptor from %s:\n", filename.c_str());
        fbl::unique_fd fd(openat(dir_fd, filename.c_str(), O_RDWR));
        if (!fd.is_valid()) {
          printer->Print("could not open %s\n", filename.c_str());
          return;
        }

        zx::channel chan;
        zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
        if (status != ZX_OK) {
          printer->Print("Fdio get handle failed with %s\n", zx_status_get_string(status));
          return;
        }

        auto device = fidl::WireSharedClient(
            fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(chan)),
            loop->dispatcher());
        status = print_input_report::PrintInputDescriptor(filename, printer, std::move(device));
        if (status != 0) {
          printer->Print("Failed to PrintInputReports\n");
          return;
        }
      });

  loop->Run();
  return 0;
};

}  // namespace print_input_report

int main(int argc, const char** argv) {
  // Register with tracing.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  zx_status_t status = loop.StartThread();
  if (status != ZX_OK) {
    printf("Error setting up tracing: %s\n", zx_status_get_string(status));
    exit(1);
  }
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

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
    // If we don't have a device path then read all devices.
    if (args.size() < 2) {
      return print_input_report::ReadAllDevices(&loop, &printer);
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
    auto client = print_input_report::GetClientFromPath(&printer, device_path, loop.dispatcher());
    if (!client) {
      return -1;
    }

    auto res = print_input_report::GetReaderClient(&client.value(), loop.dispatcher());
    if (!res.is_ok()) {
      return res.status_value();
    }
    auto reader = std::move(res.value());

    printer.Print("Reading reports from %s:\n", device_path.c_str());
    print_input_report::PrintInputReports(device_path, &printer, std::move(reader), num_reads,
                                          [&loop]() { loop.Shutdown(); });
    loop.Run();

    // The "descriptor" command.
  } else if (args[0] == "descriptor") {
    // If we don't have a device path then read all of the descriptors.
    if (args.size() < 2) {
      return ReadAllDescriptors(&loop, &printer);
    }

    const std::string& device_path = args[1].c_str();
    auto client = print_input_report::GetClientFromPath(&printer, device_path, loop.dispatcher());
    if (!client) {
      return -1;
    }
    print_input_report::PrintInputDescriptor(device_path, &printer, std::move(*client),
                                             [&loop]() { loop.Shutdown(); });

    loop.Run();
  } else {
    print_input_report::PrintHelp(&printer);
  };

  return 0;
}
