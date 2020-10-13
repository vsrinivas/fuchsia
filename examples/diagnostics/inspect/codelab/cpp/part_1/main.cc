// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
// CODELAB: Include inspect.

#include <lib/syslog/cpp/macros.h>

#include "reverser.h"

int main(int argc, char** argv) {
  // [START init_logger]
  syslog::SetTags({"inspect_cpp_codelab", "part1"});
  // [END init_logger]

  FX_LOGS(INFO) << "Starting up...";

  // Standard component setup, create an event loop and obtain the
  // ComponentContext.
  // [START async_executor]
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  // [END async_executor]

  // CODELAB: Initialize Inspect here.

  // Serve the reverser service.
  // [START serve_outgoing]
  context->outgoing()->AddPublicService(Reverser::CreateDefaultHandler());
  // [END serve_outgoing]

  // Send a request to the FizzBuzz service and print the response when it arrives.
  // [START fizzbuzz_connect]
  fuchsia::examples::inspect::FizzBuzzPtr fizz_buzz;
  context->svc()->Connect(fizz_buzz.NewRequest());
  fizz_buzz->Execute(30, [](std::string result) { FX_LOGS(INFO) << "Got FizzBuzz: " << result; });
  // [END fizzbuzz_connect]

  // Run the loop.
  loop.Run();
  return 0;
}
