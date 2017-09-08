// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/decode/decoder.h"
#include "garnet/bin/media/ffmpeg/ffmpeg_decoder.h"

namespace media {

Result Decoder::Create(const StreamType& stream_type,
                       std::shared_ptr<Decoder>* decoder_out) {
  std::shared_ptr<Decoder> decoder;
  Result result = FfmpegDecoder::Create(stream_type, &decoder);
  if (result == Result::kOk) {
    *decoder_out = decoder;
  }

  return result;
}

}  // namespace media
