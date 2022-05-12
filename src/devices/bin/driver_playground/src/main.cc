// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include "src/devices/bin/driver_playground/src/playground.h"
#include "src/devices/lib/log/log.h"

int main(int argc, char* argv[]) {
  // Initialize the async loop. The server will use the dispatcher of this
  // loop to listen for incoming requests.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an Outgoing class which will serve requests from the /svc/ directory.
  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);
  zx::status<> status = outgoing.ServeFromStartupInfo();
  if (status.is_error()) {
    LOGF(ERROR, "error: ServeFromStartupInfo returned: %s", status.status_string());
    return -1;
  }

  // Add the Playground protocol to our outgoing directory.
  auto server = std::make_unique<Playground>();
  status = outgoing.AddProtocol<fuchsia_driver_playground::ToolRunner>(server.get());
  LOGF(INFO, "Running Playground server");
  loop.Run();
  return 0;
}
