// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/mediaplayer_test_util_params.h"

#include <iostream>
#include "src/lib/fxl/strings/split_string.h"

namespace media_player {
namespace test {

MediaPlayerTestUtilParams::MediaPlayerTestUtilParams(
    const fxl::CommandLine& command_line) {
  is_valid_ = false;

  play_ = command_line.HasOption("play");
  loop_ = command_line.HasOption("loop");
  test_seek_ = command_line.HasOption("test-seek");
  experiment_ = command_line.HasOption("experiment");

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

  if (urls_.empty()) {
    Usage();
    std::cerr << "Urls/paths required\n";
    return;
  }

  if (urls_.size() > 1 && test_seek_) {
    Usage();
    std::cerr << "--test-seek only works with a single url-or-path\n";
    return;
  }

  if (loop_ && test_seek_) {
    std::cerr << "--loop and --test-seek are mutually exclusive\n";
  }

  is_valid_ = true;
}

void MediaPlayerTestUtilParams::Usage() {
  std::cerr << "mediaplayer_test_util usage:\n";
  std::cerr
      << "    present_view mediaplayer_test_util [ options ] url-or-path*\n";
  std::cerr << "options:\n";
  std::cerr << "    --play       play on startup\n";
  std::cerr << "    --loop       play the files in a loop on startup\n";
  std::cerr << "    --test-seek  play random segments of one file on startup\n";
  // --experiment is deliberately omitted here. See the .h file.
}

}  // namespace test
}  // namespace media_player
