// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <src/lib/syslog/cpp/logger.h>

#include "reverser.h"

using syslog::InitLogger;

int main(int argc, char** argv) {
  InitLogger({"inspect_cpp_codelab", "part2"});

  FX_LOGS(INFO) << "Starting up...";

  // Standard component setup, create an event loop and obtain the
  // ComponentContext.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // Create an inspector for this component.
  sys::ComponentInspector inspector(context.get());

  // Create a version string.
  // We pass the inspector along when creating the property to tie their lifecycles together.
  // It is an error to not retain the created property.
  inspector.root().CreateString("version", "part2", &inspector);

  // Serve the reverser service.
  context->outgoing()->AddPublicService(
      Reverser::CreateDefaultHandler(inspector.root().CreateChild("reverser_service")));

  // Send a request to the FizzBuzz service and print the response when it arrives.
  // CODELAB: Instrument our connection to FizzBuzz using Inspect. Is there an error?
  fuchsia::examples::inspect::FizzBuzzPtr fizz_buzz;
  context->svc()->Connect(fizz_buzz.NewRequest());
  fizz_buzz->Execute(30, [](std::string result) { FX_LOGS(INFO) << "Got FizzBuzz: " << result; });

  // Run the loop.
  loop.Run();
  return 0;
}
