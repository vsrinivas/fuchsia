// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_FACTORY_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_FACTORY_H_

#include <memory>

#include "garnet/bin/media/media_player/decode/decoder.h"

namespace media_player {

// Factory for ffmpeg decoders.
class FfmpegDecoderFactory : public DecoderFactory {
 public:
  // Creates an ffmmpeg decoder factory.
  static std::unique_ptr<DecoderFactory> Create(
      component::StartupContext* startup_context);

  FfmpegDecoderFactory();

  ~FfmpegDecoderFactory() override;

  void CreateDecoder(
      const StreamType& stream_type,
      fit::function<void(std::shared_ptr<Decoder>)> callback) override;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_FACTORY_H_
