// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/mdns/standalone/mdns_standalone.h"
#include "lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.positional_args().size() != 1) {
    std::cout << "One argument expected.";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  mdns::MdnsStandalone standalone(command_line.positional_args()[0]);

  loop.Run();
  return 0;
}
