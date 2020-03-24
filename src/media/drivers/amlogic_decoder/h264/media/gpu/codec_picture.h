// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_GPU_CODEC_PICTURE_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_GPU_CODEC_PICTURE_H_

#include <vector>
#include "chromium_utils.h"
#include "geometry.h"
#include "media/base/decrypt_config.h"
#include "media/base/video_color_space.h"
#include "media/gpu/h264_dpb.h"

namespace media {

class CodecPicture {
 public:
  CodecPicture() {}

  int32_t bitstream_id() const { return bitstream_id_; }
  void set_bitstream_id(int32_t bitstream_id) { bitstream_id_ = bitstream_id; }

  const gfx::Rect visible_rect() const { return visible_rect_; }
  void set_visible_rect(const gfx::Rect& rect) { visible_rect_ = rect; }

  // Populate with an unspecified colorspace by default.
  VideoColorSpace get_colorspace() const { return colorspace_; }
  void set_colorspace(VideoColorSpace colorspace) { colorspace_ = colorspace; }

  virtual ~CodecPicture() {}

 private:
  int32_t bitstream_id_ = -1;
  gfx::Rect visible_rect_;
  std::unique_ptr<DecryptConfig> decrypt_config_;
  VideoColorSpace colorspace_;

  DISALLOW_COPY_AND_ASSIGN(CodecPicture);
};

}  // namespace media

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_GPU_CODEC_PICTURE_H_
