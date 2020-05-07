// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
// [START part_1_include_inspect]
#include <lib/sys/inspect/cpp/component.h>
// [END part_1_include_inspect]

#include <lib/syslog/cpp/macros.h>

#include "reverser.h"

int main(int argc, char** argv) {
  syslog::SetTags({"inspect_cpp_codelab", "part2"});

  FX_LOGS(INFO) << "Starting up...";

  // Standard component setup, create an event loop and obtain the
  // ComponentContext.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Create an inspector for this component.
  // [START part_1_init_inspect]
  sys::ComponentInspector inspector(context.get());
  // [END part_1_init_inspect]

  // Create a version string.
  // We pass the inspector along when creating the property to tie their lifecycles together.
  // It is an error to not retain the created property.
  // [START part_1_write_version]
  inspector.root().CreateString("version", "part2", &inspector);
  // [END part_1_write_version]

  // Serve the reverser service.
  // [START part_1_new_child]
  context->outgoing()->AddPublicService(
      Reverser::CreateDefaultHandler(inspector.root().CreateChild("reverser_service")));
  // [END part_1_new_child]

  // Send a request to the FizzBuzz service and print the response when it arrives.
  // [START instrument_fizzbuzz]
  // CODELAB: Instrument our connection to FizzBuzz using Inspect. Is there an error?
  fuchsia::examples::inspect::FizzBuzzPtr fizz_buzz;
  context->svc()->Connect(fizz_buzz.NewRequest());
  fizz_buzz.set_error_handler([&](zx_status_t status) {
    // CODELAB: Add Inspect here to see if there is a response.
  });
  fizz_buzz->Execute(30, [](std::string result) {
    // CODELAB: Add Inspect here to see if there was a response.
    FX_LOGS(INFO) << "Got FizzBuzz: " << result;
  });
  // [END instrument_fizzbuzz]

  // Run the loop.
  loop.Run();
  return 0;
}
