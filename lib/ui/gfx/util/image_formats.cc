// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/gfx/util/image_formats.h"
#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {
namespace image_formats {

namespace {

uint8_t clip(int in) {
  uint32_t out = in < 0 ? 0 : (uint32_t)in;
  return out > 255 ? 255 : (out & 0xff);
}

// Takes 4 bytes of YUY2 and writes 8 bytes of RGBA
// TODO(MZ-547): do this better with a lookup table
void Yuy2ToBgra(uint8_t* yuy2, uint8_t* bgra1, uint8_t* bgra2) {
  int u = yuy2[1] - 128;
  int y1 = 298 * (yuy2[0] - 16);
  int v = yuy2[3] - 128;
  int y2 = 298 * (yuy2[2] - 16);
  bgra1[0] = clip(((y1 + 516 * u + 128) / 256));            // blue
  bgra1[1] = clip(((y1 - 208 * v - 100 * u + 128) / 256));  // green
  bgra1[2] = clip(((y1 + 409 * v + 128) / 256));            // red
  bgra1[3] = 0xff;                                          // alpha

  bgra2[0] = clip(((y2 + 516 * u + 128) / 256));            // blue
  bgra2[1] = clip(((y2 - 208 * v - 100 * u + 128) / 256));  // green
  bgra2[2] = clip(((y2 + 409 * v + 128) / 256));            // red
  bgra2[3] = 0xff;                                          // alpha
}

void ConvertYuy2ToBgra(uint8_t* out_ptr, uint8_t* in_ptr,
                       uint64_t buffer_size) {
  // converts to BGRA
  // uint8_t addresses:
  //   0   1   2   3   4   5   6   7   8
  // | Y | U | Y | V |
  // | B | G | R | A | B | G | R | A
  // We have 2 bytes per pixel, but we need to convert blocks of 4:
  uint32_t num_double_pixels = buffer_size / 4;
  // Since in_ptr and out_ptr are uint8_t, we step by 4 (bytes)
  // in the incoming buffer, and 8 (bytes) in the  output buffer.
  for (unsigned int i = 0; i < num_double_pixels; i++) {
    Yuy2ToBgra(&in_ptr[4 * i], &out_ptr[8 * i], &out_ptr[8 * i + 4]);
  }
}

void ConvertYuy2ToBgraAndMirror(uint8_t* out_ptr, uint8_t* in_ptr,
                                uint32_t out_width, uint32_t out_height) {
  uint32_t double_pixels_per_row = out_width / 2;
  uint32_t in_stride = out_width * 2;
  uint32_t out_stride = out_width * 4;
  // converts to BGRA and mirrors left-right
  for (uint32_t y = 0; y < out_height; ++y) {
    for (uint32_t x = 0; x < double_pixels_per_row; ++x) {
      uint64_t out = 8 * ((double_pixels_per_row - 1 - x)) + y * out_stride;
      Yuy2ToBgra(&in_ptr[4 * x + y * in_stride], &out_ptr[out + 4],
                 &out_ptr[out]);
    }
  }
}

void MirrorBgra(uint32_t* out_ptr, uint32_t* in_ptr, uint32_t width,
                uint32_t height) {
  // converts to BGRA and mirrors left-right
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint64_t out = ((width - 1 - x)) + y * width;
      out_ptr[out] = in_ptr[x + y * width];
    }
  }
}

}  // anonymous namespace

size_t BytesPerPixel(const fuchsia::images::PixelFormat& pixel_format) {
  switch (pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      return 4u;
    case fuchsia::images::PixelFormat::YUY2:
      return 2u;
  }
  FXL_CHECK(false) << "Unknown Pixel Format";
  return 0;
}

size_t PixelAlignment(const fuchsia::images::PixelFormat& pixel_format) {
  switch (pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      return 4u;
    case fuchsia::images::PixelFormat::YUY2:
      return 2u;
  }
  FXL_CHECK(false) << "Unknown Pixel Format";
  return 0;
}

escher::image_utils::ImageConversionFunction GetFunctionToConvertToBgra8(
    const fuchsia::images::ImageInfo& image_info) {
  size_t bpp = BytesPerPixel(image_info.pixel_format);
  switch (image_info.pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      if (image_info.transform == fuchsia::images::Transform::FLIP_HORIZONTAL) {
        return [](void* out, void* in, uint32_t width, uint32_t height) {
          MirrorBgra(reinterpret_cast<uint32_t*>(out),
                     reinterpret_cast<uint32_t*>(in), width, height);
        };
      } else {
        // no conversion needed.
        return [bpp](void* out, void* in, uint32_t width, uint32_t height) {
          memcpy(out, in, width * height * bpp);
        };
      }
      break;
    // TODO(MZ-551): support vertical flipping
    case fuchsia::images::PixelFormat::YUY2:
      if (image_info.transform == fuchsia::images::Transform::FLIP_HORIZONTAL) {
        return [](void* out, void* in, uint32_t width, uint32_t height) {
          ConvertYuy2ToBgraAndMirror(reinterpret_cast<uint8_t*>(out),
                                     reinterpret_cast<uint8_t*>(in), width,
                                     height);
        };
      } else {
        return [bpp](void* out, void* in, uint32_t width, uint32_t height) {
          ConvertYuy2ToBgra(reinterpret_cast<uint8_t*>(out),
                            reinterpret_cast<uint8_t*>(in),
                            width * height * bpp);
        };
      }
      break;
  }
  return nullptr;
}

}  // namespace image_formats
}  // namespace gfx
}  // namespace scenic
