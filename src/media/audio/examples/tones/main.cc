// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/examples/tones/tones.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"tones"});

  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  examples::Tones tones(command_line.HasOption("interactive"), [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  loop.Run();
  return 0;
}
