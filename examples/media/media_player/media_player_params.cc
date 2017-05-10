// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/media_player/media_player_params.h"

#include <iostream>

#include "lib/ftl/strings/split_string.h"

namespace examples {

MediaPlayerParams::MediaPlayerParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  stay_ = command_line.HasOption("stay");

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

  bool service_found = command_line.GetOptionValue("service", &service_name_);

  std::string remote;
  if (command_line.GetOptionValue("remote", &remote)) {
    if (service_found || stay_) {
      Usage();
      return;
    }

    auto split = ftl::SplitString(remote, "#", ftl::kTrimWhitespace,
                                  ftl::kSplitWantNonEmpty);

    if (split.size() != 2) {
      Usage();
      std::cerr << "Invalid --remote value\n";
      return;
    }

    device_name_ = split[0].ToString();
    service_name_ = split[1].ToString();
  } else if (!url_found && !stay_) {
    Usage();
    return;
  }

  is_valid_ = true;
}

void MediaPlayerParams::Usage() {
  std::cerr << "media_player usage:\n";
  std::cerr << "    launch media_player [ options ] [ url-or-path ]\n";
  std::cerr << "options:\n";
  std::cerr << "    --service=<service>         set the service name "
               "(default is media_player)\n";
  std::cerr << "    --remote=<device>#<service> control a remote player\n";
  std::cerr << "    --stay                      used to start the player with "
               "no content for remote control";
  std::cerr << "The --service and --remote options are mutually exclusive.\n";
  std::cerr << "A url-or-path (or --stay) is required for local playback, "
               "optional for remote.\n";
}

}  // namespace examples
