// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/demux/demux.h"

#include "src/media/playback/mediaplayer_tmp/ffmpeg/ffmpeg_demux_factory.h"

namespace media_player {

std::unique_ptr<DemuxFactory> DemuxFactory::Create(
    component::StartupContext* startup_context) {
  return FfmpegDemuxFactory::Create(startup_context);
}

}  // namespace media_player
