// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/examples/audio_player/audio_player_params.h"

#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include "src/lib/fxl/strings/split_string.h"

namespace examples {

AudioPlayerParams::AudioPlayerParams(const fxl::CommandLine& command_line) {
  is_valid_ = false;

  bool path_found = false;

  for (const std::string& arg : command_line.positional_args()) {
    if (path_found) {
      Usage();
      std::cerr << "At most one path allowed\n";
      return;
    }

    if (arg.compare(0, 1, "/") == 0) {
      path_ = arg;
      path_found = true;
    } else {
      Usage();
      std::cerr << "Path must start with '/'\n";
      return;
    }
  }

  stay_ = !path_found;
  stay_ = stay_ || command_line.HasOption("stay");

  is_valid_ = true;
}

void AudioPlayerParams::Usage() {
  std::cerr << "audio_player usage:\n";
  std::cerr << "    audio_player [ options ] [ path ]\n";
  std::cerr << "options:\n";
  std::cerr << "    --stay               don't quit at end-of-stream\n";
  std::cerr << "The audio player terminates at end-of-stream if:\n";
  std::cerr << "   a path is supplied, and\n";
  std::cerr << "   the --service option is not used, and\n";
  std::cerr << "   the --stay option is not used\n";
}

}  // namespace examples
