// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>

#include <ddk/driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "src/connectivity/bluetooth/core/bt-host/common/byte_buffer.h"
#include "src/connectivity/bluetooth/core/bt-host/common/log.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/device_wrapper.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/hci.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/transport.h"
#include "src/lib/fxl/command_line.h"
#include "src/connectivity/bluetooth/tools/lib/command_dispatcher.h"

#include "command_channel.h"
#include "commands.h"

namespace {

const char kUsageString[] =
    "Command-line tool for sending HCI Vendor commands to Intel hardware\n"
    "The behavior of this tool is undefined if used with a non-Intel "
    "controller\n"
    "\n"
    "Usage: bt_intel_tool [--dev=<bt-hci-dev>] cmd...\n"
    "    e.g. bt_intel_tool read-version";

const char kDefaultHCIDev[] = "/dev/class/bt-hci/000";

}  // namespace

// TODO(armansito): Make this tool not depend on drivers/bluetooth/lib and avoid
// this hack.
BT_DECLARE_FAKE_DRIVER();

int main(int argc, char* argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help", nullptr)) {
    std::cout << kUsageString << std::endl;
    return EXIT_SUCCESS;
  }

  auto severity = bt::common::LogSeverity::ERROR;
  if (cl.HasOption("verbose", nullptr)) {
    severity = bt::common::LogSeverity::TRACE;
  }
  bt::common::UsePrintf(severity);

  std::string hci_dev_path = kDefaultHCIDev;
  if (cl.GetOptionValue("dev", &hci_dev_path) && hci_dev_path.empty()) {
    std::cout << "Empty device path not allowed" << std::endl;
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  CommandChannel channel(hci_dev_path);

  bluetooth_tools::CommandDispatcher dispatcher;
  bt_intel::RegisterCommands(&channel, &dispatcher);

  if (cl.positional_args().empty() || cl.positional_args()[0] == "help") {
    dispatcher.DescribeAllCommands();
    return EXIT_SUCCESS;
  }

  auto complete_cb = [&loop] { loop.Shutdown(); };

  bool cmd_found;
  if (!dispatcher.ExecuteCommand(cl.positional_args(), complete_cb,
                                 &cmd_found)) {
    if (!cmd_found)
      std::cout << "Unknown command: " << cl.positional_args()[0] << std::endl;
    return EXIT_FAILURE;
  }

  loop.Run();

  return EXIT_SUCCESS;
}
