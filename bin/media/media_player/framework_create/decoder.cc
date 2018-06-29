// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/decode/decoder.h"

#include "garnet/bin/media/media_player/ffmpeg/ffmpeg_decoder_factory.h"

namespace media_player {

std::unique_ptr<DecoderFactory> DecoderFactory::Create(
    fuchsia::sys::StartupContext* startup_context) {
  return FfmpegDecoderFactory::Create(startup_context);
}

}  // namespace media_player
