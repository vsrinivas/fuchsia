// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/player/test/fake_demux.h"

#include "garnet/bin/media/framework/types/audio_stream_type.h"
#include "garnet/bin/media/framework/types/stream_type.h"
#include "garnet/bin/media/framework/types/video_stream_type.h"

using media::AudioStreamType;
using media::StreamType;
using media::TimelineRate;
using media::VideoStreamType;

namespace media_player {
namespace test {

// static
std::shared_ptr<FakeDemux> FakeDemux::Create() {
  return std::make_shared<FakeDemux>();
}

FakeDemux::FakeDemux() {
  stream_impls_.push_back(std::make_unique<DemuxStreamImpl>(
      0,
      AudioStreamType::Create(StreamType::kAudioEncodingVorbis, nullptr,
                              AudioStreamType::SampleFormat::kFloat, 2, 44100),
      TimelineRate(1, 1)));
  stream_impls_.push_back(std::make_unique<DemuxStreamImpl>(
      1,
      VideoStreamType::Create(StreamType::kVideoEncodingTheora, nullptr,
                              VideoStreamType::VideoProfile::kNotApplicable,
                              VideoStreamType::PixelFormat::kYv12,
                              VideoStreamType::ColorSpace::kNotApplicable, 1920,
                              1080, 1920, 1080, 1, 1, {}, {}),
      TimelineRate(1, 1)));

  streams_.push_back(stream_impls_[0].get());
  streams_.push_back(stream_impls_[1].get());
}

}  // namespace test
}  // namespace media_player
