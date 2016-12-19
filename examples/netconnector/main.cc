// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/examples/netconnector_example/netconnector_example_impl.h"
#include "apps/netconnector/examples/netconnector_example/netconnector_example_params.h"
#include "apps/netconnector/services/netconnector.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  examples::NetConnectorExampleParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  mtl::MessageLoop loop;

  examples::NetConnectorExampleImpl impl(&params);

  loop.Run();
  return 0;
}
