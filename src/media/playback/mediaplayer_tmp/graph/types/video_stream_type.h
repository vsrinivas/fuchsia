// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_TYPES_VIDEO_STREAM_TYPE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_TYPES_VIDEO_STREAM_TYPE_H_

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer_tmp/graph/types/stream_type.h"

namespace media_player {

// Describes the type of a video stream.
class VideoStreamType : public StreamType {
 public:
  enum class PixelFormat {
    kUnknown,
    kArgb,
    kYuy2,
    kNv12,
    kYv12,
  };

  enum class ColorSpace {
    kUnknown,
    kNotApplicable,
    kJpeg,
    kHdRec709,
    kSdRec601
  };

  // Width and height.
  struct Extent {
    Extent() : width_(0), height_(0) {}
    Extent(int width, int height) : width_(width), height_(height) {}
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

   private:
    uint32_t width_;
    uint32_t height_;
  };

  // Creates a VideoStreamType.
  static std::unique_ptr<StreamType> Create(
      const std::string& encoding, std::unique_ptr<Bytes> encoding_parameters,
      PixelFormat pixel_format, ColorSpace color_space, uint32_t width,
      uint32_t height, uint32_t coded_width, uint32_t coded_height,
      uint32_t pixel_aspect_ratio_width, uint32_t pixel_aspect_ratio_height,
      uint32_t line_stride) {
    return std::unique_ptr<StreamType>(new VideoStreamType(
        encoding, std::move(encoding_parameters), pixel_format, color_space,
        width, height, coded_width, coded_height, pixel_aspect_ratio_width,
        pixel_aspect_ratio_height, line_stride));
  }

  VideoStreamType(const std::string& encoding,
                  std::unique_ptr<Bytes> encoding_parameters,
                  PixelFormat pixel_format, ColorSpace color_space,
                  uint32_t width, uint32_t height, uint32_t coded_width,
                  uint32_t coded_height, uint32_t pixel_aspect_ratio_width,
                  uint32_t pixel_aspect_ratio_height, uint32_t line_stride);

  ~VideoStreamType() override;

  const VideoStreamType* video() const override;

  PixelFormat pixel_format() const { return pixel_format_; }

  ColorSpace color_space() const { return color_space_; }

  uint32_t width() const { return width_; }

  uint32_t height() const { return height_; }

  uint32_t coded_width() const { return coded_width_; }

  uint32_t coded_height() const { return coded_height_; }

  uint32_t pixel_aspect_ratio_width() const {
    return pixel_aspect_ratio_width_;
  }

  uint32_t pixel_aspect_ratio_height() const {
    return pixel_aspect_ratio_height_;
  }

  uint32_t line_stride() const { return line_stride_; }

  std::unique_ptr<StreamType> Clone() const override;

 private:
  PixelFormat pixel_format_;
  ColorSpace color_space_;
  uint32_t width_;
  uint32_t height_;
  uint32_t coded_width_;
  uint32_t coded_height_;
  uint32_t pixel_aspect_ratio_width_;
  uint32_t pixel_aspect_ratio_height_;
  uint32_t line_stride_;
};

// Describes a set of video stream types.
class VideoStreamTypeSet : public StreamTypeSet {
 public:
  static std::unique_ptr<StreamTypeSet> Create(
      const std::vector<std::string>& encodings, Range<uint32_t> width,
      Range<uint32_t> height) {
    return std::unique_ptr<StreamTypeSet>(
        new VideoStreamTypeSet(encodings, width, height));
  }

  VideoStreamTypeSet(const std::vector<std::string>& encodings,
                     Range<uint32_t> width, Range<uint32_t> height);

  ~VideoStreamTypeSet() override;

  const VideoStreamTypeSet* video() const override;

  Range<uint32_t> width() const { return width_; }

  Range<uint32_t> height() const { return height_; }

  std::unique_ptr<StreamTypeSet> Clone() const override;

  bool Includes(const StreamType& type) const override;

 private:
  Range<uint32_t> width_;
  Range<uint32_t> height_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_TYPES_VIDEO_STREAM_TYPE_H_
