// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

#include "src/lib/fxl/command_line.h"
#include "src/media/audio/tools/wav_recorder/wav_recorder.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = sys::ComponentContext::Create();

  media::tools::WavRecorder wav_recorder(
      fxl::CommandLineFromArgcArgv(argc, argv), [&loop]() {
        async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
      });

  wav_recorder.Run(startup_context.get());
  loop.Run();

  return 0;
}
