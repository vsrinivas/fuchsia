// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/types/video_stream_type.h"

#include <unordered_map>
#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"

namespace media_player {

VideoStreamType::VideoStreamType(
    const std::string& encoding, std::unique_ptr<Bytes> encoding_parameters,
    PixelFormat pixel_format, ColorSpace color_space, uint32_t width,
    uint32_t height, uint32_t coded_width, uint32_t coded_height,
    uint32_t pixel_aspect_ratio_width, uint32_t pixel_aspect_ratio_height,
    uint32_t line_stride)
    : StreamType(StreamType::Medium::kVideo, encoding,
                 std::move(encoding_parameters)),
      pixel_format_(pixel_format),
      color_space_(color_space),
      width_(width),
      height_(height),
      coded_width_(coded_width),
      coded_height_(coded_height),
      pixel_aspect_ratio_width_(pixel_aspect_ratio_width),
      pixel_aspect_ratio_height_(pixel_aspect_ratio_height),
      line_stride_(line_stride) {}

VideoStreamType::~VideoStreamType() {}

const VideoStreamType* VideoStreamType::video() const { return this; }

std::unique_ptr<StreamType> VideoStreamType::Clone() const {
  return Create(encoding(), SafeClone(encoding_parameters()), pixel_format(),
                color_space(), width(), height(), coded_width(), coded_height(),
                pixel_aspect_ratio_width(), pixel_aspect_ratio_height(),
                line_stride());
}

VideoStreamTypeSet::VideoStreamTypeSet(
    const std::vector<std::string>& encodings, Range<uint32_t> width,
    Range<uint32_t> height)
    : StreamTypeSet(StreamType::Medium::kVideo, encodings),
      width_(width),
      height_(height) {}

VideoStreamTypeSet::~VideoStreamTypeSet() {}

const VideoStreamTypeSet* VideoStreamTypeSet::video() const { return this; }

std::unique_ptr<StreamTypeSet> VideoStreamTypeSet::Clone() const {
  return Create(encodings(), width(), height());
}

bool VideoStreamTypeSet::Includes(const StreamType& type) const {
  if (!StreamTypeSet::Includes(type)) {
    return false;
  }

  FXL_DCHECK(type.video() != nullptr);

  return width().contains(type.video()->width()) &&
         height().contains(type.video()->height());
}

}  // namespace media_player
