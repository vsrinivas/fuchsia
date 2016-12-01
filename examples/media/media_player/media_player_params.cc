// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/media_player/media_player_params.h"

#include "lib/ftl/strings/split_string.h"

namespace examples {

MediaPlayerParams::MediaPlayerParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  bool path_found = command_line.GetOptionValue("path", &path_);
  bool url_found = command_line.GetOptionValue("url", &url_);

  if (path_found == url_found) {
    FTL_LOG(ERROR) << "Either a path or a url must be supplied, for example";
    FTL_LOG(ERROR) << "    @boot launch media_player --path=/data/video.ogv";
    FTL_LOG(ERROR) << "or";
    FTL_LOG(ERROR)
        << "    @boot launch media_player --url=http://service/audio.ogg";
    return;
  }

  is_valid_ = true;
}

}  // namespace examples
