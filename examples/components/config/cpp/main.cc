// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>

// [START imports]
#include <example_config/config.h>
// [END imports]

int main(int argc, const char* argv[], char* envp[]) {
  // [START get_config]
  // Retrieve configuration
  auto c = example_config::Config::from_args();
  // [END get_config]

  // Print greeting to the log
  FX_LOGS(INFO) << "Hello, " << c.greeting << "!";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // [START inspect]
  // Record configuration to inspect
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  sys::ComponentInspector inspector(context.get());
  c.record_to_inspect(inspector.inspector());
  // [END inspect]

  loop.Run();

  return 0;
}
// [END code]
