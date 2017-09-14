// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/netconnector/netconnector_example_impl.h"
#include "garnet/examples/netconnector/netconnector_example_params.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/netconnector/fidl/netconnector.fidl.h"

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  examples::NetConnectorExampleParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  fsl::MessageLoop loop;

  examples::NetConnectorExampleImpl impl(&params);

  loop.Run();
  return 0;
}
