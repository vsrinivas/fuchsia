// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/demux/demux.h"

#include "src/media/playback/mediaplayer/ffmpeg/ffmpeg_demux_factory.h"

namespace media_player {

std::unique_ptr<DemuxFactory> DemuxFactory::Create(ServiceProvider* service_provider) {
  return FfmpegDemuxFactory::Create(service_provider);
}

}  // namespace media_player
