// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/player/test/fake_demux.h"

#include "garnet/bin/mediaplayer/framework/types/audio_stream_type.h"
#include "garnet/bin/mediaplayer/framework/types/stream_type.h"
#include "garnet/bin/mediaplayer/framework/types/video_stream_type.h"

namespace media_player {
namespace test {

// static
std::shared_ptr<FakeDemux> FakeDemux::Create() {
  return std::make_shared<FakeDemux>();
}

FakeDemux::FakeDemux() {
  streams_.push_back(std::make_unique<DemuxStreamImpl>(
      0,
      AudioStreamType::Create(StreamType::kAudioEncodingVorbis, nullptr,
                              AudioStreamType::SampleFormat::kFloat, 2, 44100),
      media::TimelineRate(1, 1)));
  streams_.push_back(std::make_unique<DemuxStreamImpl>(
      1,
      VideoStreamType::Create(StreamType::kVideoEncodingTheora, nullptr,
                              VideoStreamType::VideoProfile::kNotApplicable,
                              VideoStreamType::PixelFormat::kYv12,
                              VideoStreamType::ColorSpace::kNotApplicable, 1920,
                              1080, 1920, 1080, 1, 1, {}, {}),
      media::TimelineRate(1, 1)));
}

}  // namespace test
}  // namespace media_player
