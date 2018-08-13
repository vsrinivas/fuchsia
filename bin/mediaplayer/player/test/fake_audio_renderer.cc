// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/player/test/fake_audio_renderer.h"

#include "garnet/bin/mediaplayer/framework/types/audio_stream_type.h"

namespace media_player {
namespace test {

// static
std::shared_ptr<FakeAudioRenderer> FakeAudioRenderer::Create() {
  return std::make_shared<FakeAudioRenderer>();
}

FakeAudioRenderer::FakeAudioRenderer() {
  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm},
      AudioStreamType::SampleFormat::kSigned16, Range<uint32_t>(1, 2),
      Range<uint32_t>(1, 88200)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm}, AudioStreamType::SampleFormat::kFloat,
      Range<uint32_t>(1, 2), Range<uint32_t>(1, 88200)));
}

}  // namespace test
}  // namespace media_player
