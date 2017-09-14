// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/audio_player/audio_player.h"
#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  examples::AudioPlayerParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  fsl::MessageLoop loop;

  examples::AudioPlayer audio_player(params);

  loop.Run();
  return 0;
}
