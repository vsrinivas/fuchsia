// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/netconnector_impl.h"
#include "garnet/bin/netconnector/netconnector_params.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  netconnector::NetConnectorParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  fsl::MessageLoop loop;

  netconnector::NetConnectorImpl impl(&params);

  loop.Run();
  return 0;
}
