// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_H_

#include <memory>

#include "garnet/bin/media/media_player/decode/decoder.h"

namespace media_player {

// Abstract base class for ffmpeg-based decoders, just the create function.
// We don't want the base class implementation here, because we don't want
// dependent targets to have to deal with ffmpeg includes.
class FfmpegDecoder : public Decoder {
 public:
  // Creates an ffmpeg-based Decoder object for a given media type.
  static Result Create(const StreamType& stream_type,
                       std::shared_ptr<Decoder>* decoder_out);

  ~FfmpegDecoder() override {}
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_H_
