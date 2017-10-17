// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"

#include "garnet/examples/media/wav_record/wav_recorder.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();
  examples::WavRecorder wav_recorder(fxl::CommandLineFromArgcArgv(argc, argv));
  wav_recorder.Run(application_context.get());
  loop.Run();

  return 0;
}
