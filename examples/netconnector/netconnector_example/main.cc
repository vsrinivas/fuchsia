// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/examples/netconnector/netconnector_example/netconnector_example_impl.h"
#include "garnet/examples/netconnector/netconnector_example/netconnector_example_params.h"
#include "lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  examples::NetConnectorExampleParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  examples::NetConnectorExampleImpl impl(&params, [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  loop.Run();
  return 0;
}
