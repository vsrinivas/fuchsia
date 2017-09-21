// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/fxl/command_line.h"

#include "sniffer.h"

namespace {

const char kUsageString[] =
    "Usage: btsnoop [options]\n"
    "Options:\n"
    "    --help            Show this help message\n"
    "    --path=<path>     The path to the snoop log file\n"
    "    --dev=<hci-dev>   Path to the HCI device (default: /dev/class/bt-hci/000)\n";

const char kDefaultHCIDev[] = "/dev/class/bt-hci/000";

}  // namespace

int main(int argc, char* argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help")) {
    std::cout << kUsageString << std::endl;
    return EXIT_SUCCESS;
  }

  std::string log_file_path;
  if (!cl.GetOptionValue("path", &log_file_path)) {
    std::cout << "A path is required" << std::endl;
    std::cout << kUsageString << std::endl;
    return EXIT_FAILURE;
  }

  std::string hci_dev_path = kDefaultHCIDev;
  cl.GetOptionValue("dev", &hci_dev_path);

  btsnoop::Sniffer sniffer(hci_dev_path, log_file_path);
  if (!sniffer.Start()) {
    std::cout << "Failed to initialize sniffer" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Sniffer stopped" << std::endl;

  return EXIT_SUCCESS;
}
