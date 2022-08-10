// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <thread>

// [START imports]
// Import the header as if it's located in the same directory as BUILD.gn:
#include "examples/components/config/cpp/example_config.h"
// [END imports]

int main(int argc, const char* argv[], char* envp[]) {
  // [START get_config]
  // Retrieve configuration
  auto c = example_config::Config::TakeFromStartupHandle();
  // [END get_config]

  // Delay our print by the configured interval.
  std::this_thread::sleep_for(std::chrono::milliseconds(c.delay_ms()));

  // Print greeting to the log
  FX_LOGS(INFO) << "Hello, " << c.greeting() << "!";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // [START inspect]
  // Record configuration to inspect
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  sys::ComponentInspector inspector(context.get());
  inspect::Node config_node = inspector.root().CreateChild("config");
  c.RecordInspect(&config_node);
  // [END inspect]

  loop.Run();

  return 0;
}
// [END code]
