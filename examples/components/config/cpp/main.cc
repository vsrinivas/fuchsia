// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>

#include <example_config/config.h>

int main(int argc, const char* argv[], char* envp[]) {
  // Retrieve configuration from process args
  auto c = example_config::Config::from_args();

  // Print configured greeting to syslog
  FX_LOGS(INFO) << "Hello, " << c.greeting << "!";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  sys::ComponentInspector inspector(context.get());
  c.record_to_inspect(&inspector);

  loop.Run();
  return 0;
}
