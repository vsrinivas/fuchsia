// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/media/fidl/audio_server.fidl.h"
#include "lib/fxl/command_line.h"
#include "lib/fsl/tasks/message_loop.h"

void usage(const char* prog_name) {
  std::cout << "Usage: " << prog_name << " [gain]\n";
  std::cout << "Sets the specified master gain in dB.  Simply report the gain"
               "if no master gain is specified.\n";
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.positional_args().size() > 1) {
    usage(argv[0]);
    return 0;
  }

  bool set_gain = false;
  float gain_target;
  if (command_line.positional_args().size() == 1) {
    const char* gain_arg = command_line.positional_args()[0].c_str();

    if (sscanf(gain_arg, "%f", &gain_target) != 1) {
      usage(argv[0]);
      return 0;
    }

    set_gain = true;
  }

  fsl::MessageLoop loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();

  auto audio_server =
      application_context->ConnectToEnvironmentService<media::AudioServer>();

  if (set_gain) {
    audio_server->SetMasterGain(gain_target);
  }

  audio_server->GetMasterGain([](float db_gain) {
    std::cout << "Master gain is currently " << std::fixed
              << std::setprecision(2) << db_gain << " dB.\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  loop.Run();
  return 0;
}
