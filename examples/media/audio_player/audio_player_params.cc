// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/audio_player/audio_player_params.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace examples {

AudioPlayerParams::AudioPlayerParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  command_line.GetOptionValue("url", &url_);
  command_line.GetOptionValue("service", &service_name_);

  is_valid_ = true;
}

void AudioPlayerParams::Usage() {
  FTL_LOG(INFO) << "audio_player usage:";
  FTL_LOG(INFO) << "    audio_player [ options ]";
  FTL_LOG(INFO) << "options:";
  FTL_LOG(INFO)
      << "    --url=<url>          play content from <url> (files urls are ok)";
  FTL_LOG(INFO) << "    --service=<service>  set the service name (default is "
                   "audio_player)";
  FTL_LOG(INFO) << "options are mutually exclusive";
}

}  // namespace examples
