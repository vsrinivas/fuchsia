// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/demux/demux.h"
#include "garnet/bin/media/media_player/ffmpeg/ffmpeg_demux.h"

namespace media_player {

std::shared_ptr<Demux> Demux::Create(std::shared_ptr<Reader> reader) {
  return FfmpegDemux::Create(reader);
}

}  // namespace media_player
