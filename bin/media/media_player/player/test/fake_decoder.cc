// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/player/test/fake_decoder.h"

#include "garnet/bin/media/media_player/framework/types/audio_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"
#include "garnet/bin/media/media_player/framework/types/video_stream_type.h"

namespace media_player {
namespace test {

// static
std::unique_ptr<StreamType> FakeDecoder::OutputStreamType(
    const StreamType& stream_type) {
  switch (stream_type.medium()) {
    case StreamType::Medium::kAudio:
      FXL_DCHECK(stream_type.audio());
      return AudioStreamType::Create(StreamType::kAudioEncodingLpcm, nullptr,
                                     stream_type.audio()->sample_format(),
                                     stream_type.audio()->channels(),
                                     stream_type.audio()->frames_per_second());
    case StreamType::Medium::kVideo:
      FXL_DCHECK(stream_type.video());
      return VideoStreamType::Create(
          StreamType::kVideoEncodingUncompressed, nullptr,
          stream_type.video()->profile(), stream_type.video()->pixel_format(),
          stream_type.video()->color_space(), stream_type.video()->width(),
          stream_type.video()->height(), stream_type.video()->coded_width(),
          stream_type.video()->coded_height(),
          stream_type.video()->pixel_aspect_ratio_width(),
          stream_type.video()->pixel_aspect_ratio_height(),
          stream_type.video()->line_stride(),
          stream_type.video()->plane_offset());
    case StreamType::Medium::kText:
    case StreamType::Medium::kSubpicture:
      FXL_DCHECK(false) << "Text and Subpicture media not supported.";
      return nullptr;
  }
}

FakeDecoderFactory::FakeDecoderFactory() {}

FakeDecoderFactory::~FakeDecoderFactory() {}

void FakeDecoderFactory::CreateDecoder(
    const StreamType& stream_type,
    fit::function<void(std::shared_ptr<Decoder>)> callback) {
  FXL_DCHECK(callback);
  callback(std::make_shared<test::FakeDecoder>(stream_type));
}

}  // namespace test

// static
std::unique_ptr<DecoderFactory> DecoderFactory::Create(
    component::StartupContext* startup_context) {
  return std::make_unique<test::FakeDecoderFactory>();
}

}  // namespace media_player
