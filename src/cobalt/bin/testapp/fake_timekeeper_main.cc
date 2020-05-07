// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This application is intended to be used while testing of
// the Cobalt logger client on Fuchsia.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include "src/cobalt/bin/testapp/fake_timekeeper.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line, {"cobalt", "faketimekeeper"});

  FX_LOGS(INFO) << "The Cobalt fake timekeeper service is starting.";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  cobalt::testapp::FakeTimekeeper app;

  int ret = loop.Run();
  return ret;
}
