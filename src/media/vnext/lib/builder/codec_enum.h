// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_BUILDER_CODEC_ENUM_H_
#define SRC_MEDIA_VNEXT_LIB_BUILDER_CODEC_ENUM_H_

#include <vector>

#include "src/media/vnext/lib/ffmpeg/av_codec_context.h"

namespace fmlib {

// |CodecEnum| methods enumerate available codecs across all supported technologies. For the
// moment, that's just ffmpeg.
// TODO(dalesat): Include codecs other than ffmpeg's.
struct CodecEnum {
  static std::vector<std::string> GetAudioDecoderCompressionTypes() {
    return AvCodecContext::GetAudioDecoderCompressionTypes();
  }

  static std::vector<std::string> GetVideoDecoderCompressionTypes() {
    return AvCodecContext::GetVideoDecoderCompressionTypes();
  }

  static std::vector<std::string> GetAudioEncoderCompressionTypes() {
    return AvCodecContext::GetAudioEncoderCompressionTypes();
  }

  static std::vector<std::string> GetVideoEncoderCompressionTypes() {
    return AvCodecContext::GetVideoEncoderCompressionTypes();
  }
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_BUILDER_CODEC_ENUM_H_
