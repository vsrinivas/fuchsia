// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/mediaplayer_test_util_params.h"

#include <iostream>

#include "src/lib/fxl/strings/split_string.h"

namespace media_player {
namespace test {

MediaPlayerTestUtilParams::MediaPlayerTestUtilParams(const fxl::CommandLine& command_line) {
  is_valid_ = false;

  play_ = command_line.HasOption("play");
  loop_ = command_line.HasOption("loop");
  test_seek_ = command_line.HasOption("test-seek");
  experiment_ = command_line.HasOption("experiment");

  std::string rate_as_string;
  if (command_line.GetOptionValue("rate", &rate_as_string)) {
    char* end;
    rate_ = std::strtof(rate_as_string.c_str(), &end);
    if (end == rate_as_string.c_str() || *end != '\0') {
      Usage();
      std::cerr << "Unrecognized --rate value\n";
      return;
    }

    if (rate_ <= 0.0f) {
      Usage();
      std::cerr << "--rate value must be positive\n";
      return;
    }
  }

  for (const std::string& arg : command_line.positional_args()) {
    if (arg.compare(0, 1, "/") == 0) {
      paths_.push_back(arg);
    } else {
      Usage();
      std::cerr << "Path must start with '/'\n";
      return;
    }
  }

  if (paths_.empty()) {
    Usage();
    std::cerr << "Paths required\n";
    return;
  }

  if (paths_.size() > 1 && test_seek_) {
    Usage();
    std::cerr << "--test-seek only works with a single path\n";
    return;
  }

  if (loop_ && test_seek_) {
    std::cerr << "--loop and --test-seek are mutually exclusive\n";
  }

  is_valid_ = true;
}

void MediaPlayerTestUtilParams::Usage() {
  std::cerr << "mediaplayer_test_util usage:\n";
  std::cerr << "    present_view mediaplayer_test_util [ options ] path*\n";
  std::cerr << "options:\n";
  std::cerr << "    --play        play on startup\n";
  std::cerr << "    --loop        play the files in a loop on startup\n";
  std::cerr << "    --test-seek   play random segments of one file on startup\n";
  std::cerr << "    --rate=<rate> sets the playback rate (default is 1.0)\n";
  // --experiment is deliberately omitted here. See the .h file.
}

}  // namespace test
}  // namespace media_player
