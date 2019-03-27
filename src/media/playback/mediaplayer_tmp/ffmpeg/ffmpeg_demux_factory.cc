// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/ffmpeg/ffmpeg_demux_factory.h"

#include "src/media/playback/mediaplayer_tmp/ffmpeg/ffmpeg_demux.h"

namespace media_player {

// static
std::unique_ptr<DemuxFactory> FfmpegDemuxFactory::Create(
    component::StartupContext* startup_context) {
  return std::make_unique<FfmpegDemuxFactory>();
}

FfmpegDemuxFactory::FfmpegDemuxFactory() {}

FfmpegDemuxFactory::~FfmpegDemuxFactory() {}

// Creates a |Demux| object for a given reader.
Result FfmpegDemuxFactory::CreateDemux(
    std::shared_ptr<ReaderCache> reader_cache,
    std::shared_ptr<Demux>* demux_out) {
  FXL_DCHECK(reader_cache);
  FXL_DCHECK(demux_out);

  *demux_out = FfmpegDemux::Create(reader_cache);

  return Result::kOk;
}

}  // namespace media_player
