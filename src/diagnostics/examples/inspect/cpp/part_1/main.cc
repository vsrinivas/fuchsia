// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
// CODELAB: Include inspect.

#include <src/lib/syslog/cpp/logger.h>

#include "reverser.h"

using syslog::InitLogger;

int main(int argc, char** argv) {
  InitLogger({"inspect_cpp_codelab", "part1"});

  FX_LOGS(INFO) << "Starting up...";

  // Standard component setup, create an event loop and obtain the
  // ComponentContext.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // CODELAB: Initialize Inspect here.

  // Serve the reverser service.
  context->outgoing()->AddPublicService(Reverser::CreateDefaultHandler());

  // Send a request to the FizzBuzz service and print the response when it arrives.
  fuchsia::examples::inspect::FizzBuzzPtr fizz_buzz;
  context->svc()->Connect(fizz_buzz.NewRequest());
  fizz_buzz->Execute(30, [](std::string result) { FX_LOGS(INFO) << "Got FizzBuzz: " << result; });

  // Run the loop.
  loop.Run();
  return 0;
}
