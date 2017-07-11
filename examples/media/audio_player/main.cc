// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/audio_player/audio_player.h"
#include "apps/media/examples/audio_player/audio_player_params.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  examples::AudioPlayerParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  mtl::MessageLoop loop;

  examples::AudioPlayer audio_player(params);

  loop.Run();
  return 0;
}
