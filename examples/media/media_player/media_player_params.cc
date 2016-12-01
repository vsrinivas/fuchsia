// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/media_player/media_player_params.h"

#include "lib/ftl/strings/split_string.h"

namespace examples {

MediaPlayerParams::MediaPlayerParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  if (!command_line.GetOptionValue("path", &path_)) {
    FTL_LOG(ERROR) << "Path must be supplied, e.g. --path=/data/vid.ogv";
    return;
  }

  is_valid_ = true;
}

}  // namespace examples
