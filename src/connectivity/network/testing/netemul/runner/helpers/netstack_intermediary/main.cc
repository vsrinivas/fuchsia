// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>

#include <iostream>

#include "netstack_intermediary.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

static void usage() {
  std::cerr << "Usage: netstack_intermediary --network=<virtual_network_name>";
}

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  syslog::InitLogger();

  if (!command_line.HasOption("network")) {
    FXL_LOG(ERROR) << "Invalid args.";
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  std::string network_name;
  command_line.GetOptionValue("network", &network_name);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());
  NetstackIntermediary netstack_intermediary(std::move(network_name));
  loop.Run();
}
