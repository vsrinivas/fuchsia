// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/media_player/media_player_params.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace examples {

MediaPlayerParams::MediaPlayerParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  command_line.GetOptionValue("url", &url_);
  bool service_found = command_line.GetOptionValue("service", &service_name_);

  std::string remote;
  if (command_line.GetOptionValue("remote", &remote)) {
    if (service_found) {
      Usage();
      return;
    }

    auto split = ftl::SplitString(remote, "#", ftl::kTrimWhitespace,
                                  ftl::kSplitWantNonEmpty);

    if (split.size() != 2) {
      Usage();
      FTL_LOG(ERROR) << "Invalid --remote value";
      return;
    }

    device_name_ = split[0].ToString();
    service_name_ = split[1].ToString();
  }

  is_valid_ = true;
}

void MediaPlayerParams::Usage() {
  FTL_LOG(INFO) << "media_player usage:";
  FTL_LOG(INFO) << "    launch media_player [ options ]";
  FTL_LOG(INFO) << "options:";
  FTL_LOG(INFO) << "    --url=<url>                 read content from <url> "
                   "(file urls are ok)";
  FTL_LOG(INFO) << "    --service=<service>         set the service name "
                   "(default is media_player)";
  FTL_LOG(INFO) << "    --remote=<device>#<service> control a remote player";
  FTL_LOG(INFO) << "The --service and --remote options are mutually exclusive.";
}

}  // namespace examples
