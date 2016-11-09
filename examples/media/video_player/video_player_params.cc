// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/video_player/video_player_params.h"

#include "lib/ftl/strings/split_string.h"

namespace examples {

VideoPlayerParams::VideoPlayerParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  if (!command_line.GetOptionValue("path", &path_)) {
    FTL_LOG(ERROR) << "URL must have query, e.g. fidl:video_player?path=<path>";
    return;
  }

  is_valid_ = true;
}

}  // namespace examples
