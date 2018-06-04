// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/examples/media/simple_sine/simple_sine.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/command_line.h"

namespace {
constexpr char kFloatFormatSwitch[] = "float";
}  // namespace

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto startup_context = fuchsia::sys::StartupContext::CreateFromStartupInfo();

  examples::MediaApp media_app(
      [&loop]() { async::PostTask(loop.async(), [&loop]() { loop.Quit(); }); });
  if (command_line.HasOption(kFloatFormatSwitch)) {
    media_app.set_float(true);
  }

  media_app.Run(startup_context.get());

  // We've set everything going. Wait for our message loop to return.
  loop.Run();

  return 0;
}
