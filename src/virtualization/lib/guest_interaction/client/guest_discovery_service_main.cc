// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/virtualization/lib/guest_interaction/client/guest_discovery_service.h"

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());

  // Create the guest interaction service and run its gRPC processing loop on
  // a separate thread.
  GuestDiscoveryServiceImpl guest_discovery_service = GuestDiscoveryServiceImpl();

  return loop.Run();
}
