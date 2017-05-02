// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "lib/ftl/command_line.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/strings/string_printf.h"

#include "le_connection_test.h"

namespace {

const char kUsageString[] =
    "Usage: hci_acl_test [options] [public BD_ADDR]\n"
    "Options:\n"
    "    --help            Show this help message\n"
    "    --dev=<hci-dev>   Path to the HCI device (default: %s)\n";

const char kDefaultHCIDev[] = "/dev/class/bt-hci/000";

void PrintUsage() {
  std::cout << ftl::StringPrintf(kUsageString, kDefaultHCIDev) << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  auto cl = ftl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help", nullptr)) {
    PrintUsage();
    return EXIT_SUCCESS;
  }

  if (cl.positional_args().size() != 1) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  // By default suppress all log messages below the LOG_ERROR level.
  ftl::LogSettings log_settings;
  log_settings.min_log_level = ftl::LOG_INFO;
  if (!ftl::ParseLogSettings(cl, &log_settings)) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  ftl::SetLogSettings(log_settings);

  bluetooth::common::DeviceAddressBytes dst_addr;
  if (!dst_addr.SetFromString(cl.positional_args()[0])) {
    std::cout << "Invalid BD_ADDR: " << cl.positional_args()[0] << std::endl;
    return EXIT_FAILURE;
  }

  std::string hci_dev_path = kDefaultHCIDev;
  cl.GetOptionValue("dev", &hci_dev_path);

  ftl::UniqueFD hci_dev(open(hci_dev_path.c_str(), O_RDWR));
  if (!hci_dev.is_valid()) {
    std::perror("Failed to open HCI device");
    return EXIT_FAILURE;
  }

  hci_acl_test::LEConnectionTest le_conn_test;
  if (!le_conn_test.Run(std::move(hci_dev), dst_addr)) {
    std::cout << "LE Connection Test failed" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
