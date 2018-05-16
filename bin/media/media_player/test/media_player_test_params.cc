// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/media_player_test_params.h"

#include <iostream>

#include "lib/fxl/strings/split_string.h"

namespace media_player {
namespace test {

MediaPlayerTestParams::MediaPlayerTestParams(
    const fxl::CommandLine& command_line) {
  is_valid_ = false;

  loop_ = command_line.HasOption("loop");

  for (const std::string& arg : command_line.positional_args()) {
    if (arg.compare(0, 1, "/") == 0) {
      std::string url = "file://";
      url.append(arg);
      urls_.push_back(url);
    } else if (arg.compare(0, 7, "http://") == 0 ||
               arg.compare(0, 8, "https://") == 0 ||
               arg.compare(0, 8, "file:///") == 0) {
      urls_.push_back(arg);
    } else {
      Usage();
      std::cerr << "Url-or-path must start with '/' 'http://', 'https://' or "
                   "'file:///'\n";
      return;
    }
  }

  if (urls_.empty() && loop_) {
    Usage();
    std::cerr << "Urls/paths required for --loop option\n";
    return;
  }

  is_valid_ = true;
}

void MediaPlayerTestParams::Usage() {
  std::cerr << "media_player_tests usage:\n";
  std::cerr
      << "    set_root_view media_player_tests [ options ] url-or-path*\n";
  std::cerr << "options:\n";
  std::cerr << "    --loop      play the files in a loop\n";
  std::cerr << "For CQ test, run with no arguments\n";
}

}  // namespace test
}  // namespace media_player
