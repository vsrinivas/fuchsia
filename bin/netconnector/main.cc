// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/netconnector_impl.h"
#include "apps/netconnector/src/netconnector_params.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  netconnector::NetConnectorParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  mtl::MessageLoop loop;

  netconnector::NetConnectorImpl impl(&params);

  loop.Run();
  return 0;
}
