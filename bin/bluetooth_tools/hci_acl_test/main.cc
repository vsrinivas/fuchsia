// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/strings/string_printf.h"

#include "le_connection_test.h"

namespace {

const char kUsageString[] =
    "Usage: hci_acl_test [options] [public|random] [BD_ADDR]\n"
    "Options:\n"
    "    --help            Show this help message\n"
    "    --cancel          Cancel the connection attempt right away\n"
    "    --dev=<hci-dev>   Path to the HCI device (default: %s)\n";

const char kDefaultHCIDev[] = "/dev/class/bt-hci/000";

void PrintUsage() {
  std::cout << fxl::StringPrintf(kUsageString, kDefaultHCIDev) << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help", nullptr)) {
    PrintUsage();
    return EXIT_SUCCESS;
  }

  if (cl.positional_args().size() != 2) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  // By default suppress all log messages below the LOG_ERROR level.
  fxl::LogSettings log_settings;
  log_settings.min_log_level = fxl::LOG_INFO;
  if (!fxl::ParseLogSettings(cl, &log_settings)) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  fxl::SetLogSettings(log_settings);

  bluetooth::common::DeviceAddress::Type addr_type;
  std::string addr_type_str = cl.positional_args()[0];
  if (addr_type_str == "public") {
    addr_type = bluetooth::common::DeviceAddress::Type::kLEPublic;
  } else if (addr_type_str == "random") {
    addr_type = bluetooth::common::DeviceAddress::Type::kLERandom;
  } else {
    std::cout << "Invalid address type: " << addr_type_str << std::endl;
  }

  bluetooth::common::DeviceAddressBytes addr_bytes;
  if (!addr_bytes.SetFromString(cl.positional_args()[1])) {
    std::cout << "Invalid BD_ADDR: " << cl.positional_args()[1] << std::endl;
    return EXIT_FAILURE;
  }

  std::string hci_dev_path = kDefaultHCIDev;
  cl.GetOptionValue("dev", &hci_dev_path);

  fxl::UniqueFD hci_dev(open(hci_dev_path.c_str(), O_RDWR));
  if (!hci_dev.is_valid()) {
    std::perror("Failed to open HCI device");
    return EXIT_FAILURE;
  }

  bool cancel_right_away = false;
  if (cl.HasOption("cancel", nullptr)) cancel_right_away = true;

  hci_acl_test::LEConnectionTest le_conn_test;
  if (!le_conn_test.Run(std::move(hci_dev), bluetooth::common::DeviceAddress(addr_type, addr_bytes),
                        cancel_right_away)) {
    std::cout << "LE Connection Test failed" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
