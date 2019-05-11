// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <sys/stat.h>

#include <cstdio>
#include <iostream>

#include "commands.h"
#include "lib/fit/defer.h"
#include "src/connectivity/bluetooth/core/bt-host/common/byte_buffer.h"
#include "src/connectivity/bluetooth/core/bt-host/common/log.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/device_wrapper.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/hci.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/transport.h"
#include "src/connectivity/bluetooth/tools/lib/command_dispatcher.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/command_line.h"

namespace {

const char kUsageString[] =
    "Usage: hcitool [--dev=<bt-hci-dev>] cmd...\n"
    "    e.g. hcitool reset";

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

  auto severity = bt::LogSeverity::ERROR;
  if (cl.HasOption("verbose", nullptr)) {
    severity = bt::LogSeverity::TRACE;
  }
  bt::UsePrintf(severity);

  std::string hci_dev_path = kDefaultHCIDev;
  if (cl.GetOptionValue("dev", &hci_dev_path) && hci_dev_path.empty()) {
    std::cout << "Empty device path not allowed" << std::endl;
    return EXIT_FAILURE;
  }

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    std::perror("Failed to open HCI device. Could not create fidl channel");
    return EXIT_FAILURE;
  }
  status = fdio_service_connect(hci_dev_path.c_str(), remote.release());
  if (status != ZX_OK) {
    std::perror("Failed to open HCI device");
    return EXIT_FAILURE;
  }

  auto hci_dev =
      std::make_unique<::bt::hci::FidlDeviceWrapper>(std::move(local));
  auto hci = ::bt::hci::Transport::Create(std::move(hci_dev));
  if (!hci->Initialize()) {
    return EXIT_FAILURE;
  }

  // Make sure the HCI Transport gets shut down cleanly upon exit.
  auto ac = fit::defer([&hci] { hci->ShutDown(); });

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  bluetooth_tools::CommandDispatcher dispatcher;
  hcitool::CommandData cmd_data(hci->command_channel(), loop.dispatcher());
  RegisterCommands(&cmd_data, &dispatcher);

  if (cl.positional_args().empty() || cl.positional_args()[0] == "help") {
    dispatcher.DescribeAllCommands();
    return EXIT_SUCCESS;
  }

  auto complete_cb = [&] { loop.Shutdown(); };

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
