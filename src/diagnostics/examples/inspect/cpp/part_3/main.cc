// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "reverser.h"

int main(int argc, char** argv) {
  syslog::SetTags({"inspect_cpp_codelab", "part3"});

  FX_LOGS(INFO) << "Starting up...";

  // Standard component setup, create an event loop and obtain the
  // ComponentContext.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Create an inspector for this component.
  sys::ComponentInspector inspector(context.get());

  // ComponentInspector has built-in health checking. Set it to "starting up" so snapshots show we
  // may still be initializing.
  inspector.Health().StartingUp();

  // Create a version string.
  // We pass the inspector along when creating the property to tie their lifecycles together.
  // It is an error to not retain the created property.
  inspector.root().CreateString("version", "part3", &inspector);

  // Serve the reverser service.
  context->outgoing()->AddPublicService(
      Reverser::CreateDefaultHandler(inspector.root().CreateChild("reverser_service")));

  // Send a request to the FizzBuzz service and print the response when it arrives.
  fuchsia::examples::inspect::FizzBuzzPtr fizz_buzz;
  context->svc()->Connect(fizz_buzz.NewRequest());

  // Create an error handler for the FizzBuzz service.
  fizz_buzz.set_error_handler([&](zx_status_t status) {
    char message[256];
    snprintf(message, 256, "FizzBuzz connection closed: %s", zx_status_get_string(status));
    inspector.Health().Unhealthy(message);
  });

  fizz_buzz->Execute(30, [&](std::string result) {
    // Once we get FizzBuzz response, set health to OK.
    inspector.Health().Ok();
    FX_LOGS(INFO) << "Got FizzBuzz: " << result;
  });

  // Run the loop.
  loop.Run();
  return 0;
}
