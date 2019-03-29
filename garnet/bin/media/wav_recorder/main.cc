// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <iostream>

#include "garnet/bin/media/wav_recorder/wav_recorder.h"
#include "lib/component/cpp/startup_context.h"
#include "src/lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  media::tools::WavRecorder wav_recorder(
      fxl::CommandLineFromArgcArgv(argc, argv), [&loop]() {
        async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
      });

  wav_recorder.Run(startup_context.get());
  loop.Run();

  return 0;
}
