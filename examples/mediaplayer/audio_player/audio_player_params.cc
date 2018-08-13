// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/audio_player/audio_player_params.h"

#include <iostream>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

namespace examples {

AudioPlayerParams::AudioPlayerParams(const fxl::CommandLine& command_line) {
  is_valid_ = false;

  bool url_found = false;

  for (const std::string& arg : command_line.positional_args()) {
    if (url_found) {
      Usage();
      std::cerr << "At most one url-or-path allowed\n";
      return;
    }

    if (arg.compare(0, 1, "/") == 0) {
      url_ = "file://";
      url_.append(arg);
      url_found = true;
    } else if (arg.compare(0, 7, "http://") == 0 ||
               arg.compare(0, 8, "https://") == 0 ||
               arg.compare(0, 8, "file:///") == 0) {
      url_ = arg;
      url_found = true;
    } else {
      Usage();
      std::cerr << "Url-or-path must start with '/' 'http://', 'https://' or "
                   "'file:///'\n";
      return;
    }
  }

  stay_ = !url_found;
  stay_ = stay_ || command_line.HasOption("stay");

  is_valid_ = true;
}

void AudioPlayerParams::Usage() {
  std::cerr << "audio_player usage:\n";
  std::cerr << "    audio_player [ options ] [ url-or-path ]\n";
  std::cerr << "options:\n";
  std::cerr << "    --stay               don't quit at end-of-stream\n";
  std::cerr << "The audio player terminates at end-of-stream if:\n";
  std::cerr << "   a url-or-path is supplied, and\n";
  std::cerr << "   the --service option is not used, and\n";
  std::cerr << "   the --stay option is not used\n";
}

}  // namespace examples
