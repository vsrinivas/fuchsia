// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>

#include "garnet/bin/guest/pkg/biscotti_guest/bin/guest.h"

void PrintUsage();

int main(int argc, const char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (cl.HasOption("help")) {
    PrintUsage();
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  std::unique_ptr<biscotti::Guest> guest;
  zx_status_t status = biscotti::Guest::CreateAndStart(context.get(), std::move(cl), &guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start guest: " << status;
    return -1;
  }
  loop.Run();
  return 0;
}

void PrintUsage() {
  std::cout <<
  "Usage: run biscotti [options]\n"
  "\n"
  "Options:\n"
  "--help            print this message and exit.\n"
  "--ip <addr>       configure the guest to use ip <addr>.\n"
  "--netmask <addr>  configure the guest to use netmask <addr>.\n"
  "--gateway <addr>  configure the guest to use gateway <addr>.\n"
  "\n"
  "If none of 'ip', 'netmask', or 'gateway' are provided, then the guest network\n"
  "will be left unconfigured. You'll need to configure networking on the first\n"
  "to setup the debian container.\n"
  << std::endl;
}
