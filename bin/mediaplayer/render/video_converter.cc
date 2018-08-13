// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/render/video_converter.h"

#include <trace/event.h>

namespace media_player {

VideoConverter::VideoConverter() { BuildColorspaceTable(); }

VideoConverter::~VideoConverter() {}

namespace {

uint8_t ToByte(float f) {
  if (f < 0.0f) {
    return 0u;
  }

  if (f > 255.0f) {
    return 255u;
  }

  return static_cast<uint8_t>(f);
}

size_t ColorspaceTableOffset(uint8_t y, uint8_t u, uint8_t v) {
  return (y << 8u | u) << 8u | v;
}

}  // namespace

void VideoConverter::BuildColorspaceTable() {
  colorspace_table_.reset(new uint32_t[256 * 256 * 256]);

  uint32_t* p = colorspace_table_.get();

  for (size_t iy = 0; iy < 256; ++iy) {
    for (size_t iu = 0; iu < 256; ++iu) {
      for (size_t iv = 0; iv < 256; ++iv) {
        float y = static_cast<float>(iy);
        float u = static_cast<float>(iu);
        float v = static_cast<float>(iv);

        // R = 1.164(Y - 16) + 1.596(V - 128)
        uint8_t r = ToByte(1.164f * (y - 16.0f) + 1.596f * (v - 128.0f));

        // G = 1.164(Y - 16) - 0.813(V - 128) - 0.391(U - 128)
        uint8_t g = ToByte(1.164f * (y - 16.0f) - 0.813f * (v - 128.0f) -
                           0.391f * (u - 128.0f));

        // B = 1.164(Y - 16) + 2.018(U - 128)
        uint8_t b = ToByte(1.164f * (y - 16.0f) + 2.018f * (u - 128.0f));

        *p = r | (g << 8u) | (b << 16u) | (255u << 24u);
        ++p;
      }
    }
  }
}

void VideoConverter::SetStreamType(std::unique_ptr<StreamType> stream_type) {
  stream_type_ = std::move(stream_type);
  FXL_DCHECK(stream_type_->medium() == StreamType::Medium::kVideo);
  video_stream_type_ = stream_type_->video();
  FXL_DCHECK(video_stream_type_);

  FXL_DCHECK(video_stream_type_->pixel_format() ==
             VideoStreamType::PixelFormat::kYv12)
      << "only YV12 video conversion is currently implemented";
}

fuchsia::math::Size VideoConverter::GetSize() const {
  fuchsia::math::Size size;

  if (video_stream_type_ != nullptr) {
    size.width = video_stream_type_->width();
    size.height = video_stream_type_->height();
  } else {
    size.width = 0;
    size.height = 0;
  }

  return size;
}

fuchsia::math::Size VideoConverter::GetPixelAspectRatio() const {
  fuchsia::math::Size pixel_aspect_ratio;

  if (video_stream_type_ != nullptr) {
    pixel_aspect_ratio.width = video_stream_type_->pixel_aspect_ratio_width();
    pixel_aspect_ratio.height = video_stream_type_->pixel_aspect_ratio_height();
  } else {
    pixel_aspect_ratio.width = 1;
    pixel_aspect_ratio.height = 1;
  }

  return pixel_aspect_ratio;
}

void VideoConverter::ConvertFrame(uint8_t* rgba_buffer, uint32_t view_width,
                                  uint32_t view_height, void* payload,
                                  uint64_t payload_size) {
  TRACE_DURATION("motown", "ConvertFrame");
  FXL_DCHECK(rgba_buffer != nullptr);
  FXL_DCHECK(view_width != 0);
  FXL_DCHECK(view_height != 0);
  FXL_DCHECK(payload != nullptr);
  FXL_DCHECK(payload_size != 0);
  FXL_DCHECK(video_stream_type_ != nullptr)
      << "need to call SetStreamType before ConvertFrame";

  uint32_t height = std::min(video_stream_type_->height(), view_height);
  uint32_t width = std::min(video_stream_type_->width(), view_width);

  // YV12 frames have three separate planes. The Y plane has 8-bit Y values for
  // each pixel. The U and V planes have 8-bit U and V values for 2x2 grids of
  // pixels, so those planes are each 1/4 the size of the Y plane. Both the
  // inner and outer loops below are unrolled to deal with the 2x2 logic.

  size_t dest_line_stride = view_width;
  size_t y_line_stride = video_stream_type_->line_stride_for_y_plane();
  size_t u_line_stride = video_stream_type_->line_stride_for_u_plane();
  size_t v_line_stride = video_stream_type_->line_stride_for_v_plane();

  uint32_t* dest_line = reinterpret_cast<uint32_t*>(rgba_buffer);
  uint8_t* y_line = reinterpret_cast<uint8_t*>(payload) +
                    video_stream_type_->plane_offset_for_y_plane();
  uint8_t* u_line = reinterpret_cast<uint8_t*>(payload) +
                    video_stream_type_->plane_offset_for_u_plane();
  uint8_t* v_line = reinterpret_cast<uint8_t*>(payload) +
                    video_stream_type_->plane_offset_for_v_plane();

  for (uint32_t line = 0; line < height; ++line) {
    ConvertLine(dest_line, y_line, u_line, v_line, width);

    dest_line += dest_line_stride;
    y_line += y_line_stride;
    // Notice we aren't updating u_line and v_line here.

    // If we hadn't unrolled the loop, it would have ended here.
    if (++line == height) {
      break;
    }

    ConvertLine(dest_line, y_line, u_line, v_line, width);

    dest_line += dest_line_stride;
    y_line += y_line_stride;
    // Here, we ARE updating u_line and v_line, because we've moved vertically
    // out of the 2x2 grid.
    u_line += u_line_stride;
    v_line += v_line_stride;
  }
}

void VideoConverter::ConvertLine(uint32_t* dest_pixel, uint8_t* y_pixel,
                                 uint8_t* u_pixel, uint8_t* v_pixel,
                                 uint32_t width) {
  for (uint32_t pixel = 0; pixel < width; ++pixel) {
    *dest_pixel =
        colorspace_table_
            .get()[ColorspaceTableOffset(*y_pixel, *u_pixel, *v_pixel)];
    ++dest_pixel;
    ++y_pixel;
    // Notice we aren't incrementing u_pixel and v_pixel here.

    // If we hadn't unrolled the loop, it would have ended here.
    if (++pixel == width) {
      break;
    }

    *dest_pixel =
        colorspace_table_
            .get()[ColorspaceTableOffset(*y_pixel, *u_pixel, *v_pixel)];
    ++dest_pixel;
    ++y_pixel;
    // Here, we ARE incrementing u_pixel and v_pixel, because we've moved
    // horizontally out of the 2x2 grid.
    ++u_pixel;
    ++v_pixel;
  }
}

}  // namespace media_player
