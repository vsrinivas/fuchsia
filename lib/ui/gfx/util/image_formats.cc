// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/gfx/util/image_formats.h"
#include "lib/fxl/logging.h"
#include "lib/images/images_util.h"
#include "lib/images/yuv_util.h"

namespace scenic {
namespace gfx {
namespace image_formats {

namespace {

// Takes 4 bytes of YUY2 and writes 8 bytes of RGBA
// TODO(MZ-547): do this better with a lookup table
void Yuy2ToBgra(uint8_t* yuy2, uint8_t* bgra1, uint8_t* bgra2) {
  uint8_t y1 = yuy2[0];
  uint8_t u = yuy2[1];
  uint8_t y2 = yuy2[2];
  uint8_t v = yuy2[3];
  yuv_util::YuvToBgra(y1, u, v, bgra1);
  yuv_util::YuvToBgra(y2, u, v, bgra2);
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

// For now, copy each UV sample to a 2x2 square of ouput pixels.  This is not
// proper signal processing for the UV up-scale, but it _may_ be faster.
//
// This function isn't really optimized in any serious sense so far.
void ConvertNv12ToBgra(uint8_t* out_ptr, uint8_t* in_ptr, uint32_t width,
                       uint32_t height, uint32_t in_stride) {
  uint8_t* y_base = in_ptr;
  uint8_t* uv_base = in_ptr + height * in_stride;

  // Convert 2 lines at a time, to avoid reading UV data twice.  I don't know if
  // avoiding reading UV twice really matters much since we're not skipping
  // caches (such as with non-temporal reads), and I wouldn't be surpised if the
  // bottleneck is often compute rather than memory.
  //
  // Writing two lines at a time might turn out to be counterproductive,
  // possibly depending on CPU write buffering details.
  for (uint32_t y = 0; y < height; y += 2) {
    uint8_t* y1_sample_iter = y_base + y * in_stride;
    uint8_t* y2_sample_iter = y_base + (y + 1) * in_stride;
    uint8_t* uv_sample_iter = uv_base + y / 2 * in_stride;
    uint8_t* bgra1_sample_iter = out_ptr + y * width * 4;
    uint8_t* bgra2_sample_iter = out_ptr + (y + 1) * width * 4;

    // Minimizing this inner loop matters more than per-2-lines stuff above, of
    // course.
    for (uint32_t x = 0; x < width; x += 2) {
      uint8_t u = *uv_sample_iter;
      uint8_t v = *(uv_sample_iter + 1);

      // Unknown whether unrolling this 2 pixel wide loop (by just having two
      // copies of the loop body in a row) would be better or worse.  The 2
      // pixels high is already "unrolled" in some sense, so this chunk of code
      // is processing 2x2 pixels.  For now, it's probably more readable with
      // this loop present instead of unrolled, but note that the x_offset is
      // not used within the body of the loop.
      for (uint32_t x_offset = 0; x_offset < 2; ++x_offset) {
        // Unknown whether inlining each of these is better or worse.
        yuv_util::YuvToBgra(*y1_sample_iter, u, v, bgra1_sample_iter);
        yuv_util::YuvToBgra(*y2_sample_iter, u, v, bgra2_sample_iter);
        y1_sample_iter += sizeof(uint8_t);
        y2_sample_iter += sizeof(uint8_t);
        bgra1_sample_iter += sizeof(uint32_t);
        bgra2_sample_iter += sizeof(uint32_t);
      }

      uv_sample_iter += sizeof(uint16_t);  // Each UV sample is 2 bytes.
    }
  }
}

}  // anonymous namespace

escher::image_utils::ImageConversionFunction GetFunctionToConvertToBgra8(
    const fuchsia::images::ImageInfo& image_info) {
  size_t bits_per_pixel = images_util::BitsPerPixel(image_info.pixel_format);
  switch (image_info.pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      if (image_info.transform == fuchsia::images::Transform::FLIP_HORIZONTAL) {
        return [](void* out, void* in, uint32_t width, uint32_t height) {
          MirrorBgra(reinterpret_cast<uint32_t*>(out),
                     reinterpret_cast<uint32_t*>(in), width, height);
        };
      } else {
        // no conversion needed.
        FXL_DCHECK(bits_per_pixel % 8 == 0);
        size_t bytes_per_pixel = bits_per_pixel / 8;
        return [bytes_per_pixel](void* out, void* in, uint32_t width,
                                 uint32_t height) {
          memcpy(out, in, width * height * bytes_per_pixel);
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
        FXL_DCHECK(bits_per_pixel % 8 == 0);
        size_t bytes_per_pixel = bits_per_pixel / 8;
        return [bytes_per_pixel](void* out, void* in, uint32_t width,
                                 uint32_t height) {
          ConvertYuy2ToBgra(reinterpret_cast<uint8_t*>(out),
                            reinterpret_cast<uint8_t*>(in),
                            width * height * bytes_per_pixel);
        };
      }
      break;
    case fuchsia::images::PixelFormat::NV12:
      FXL_DCHECK(image_info.transform == fuchsia::images::Transform::NORMAL)
          << "NV12 transforms not yet implemented";
      // At least for now, capture stride from the image_info. Assert that width
      // and height could also be captured this way, but don't actually use
      // their captured versions yet.
      return [captured_in_stride = image_info.stride,
              captured_width = image_info.width,
              captured_height = image_info.height](
                 void* out, void* in, uint32_t width, uint32_t height) {
        FXL_DCHECK(captured_width == width);
        FXL_DCHECK(captured_height == height);
        ConvertNv12ToBgra(reinterpret_cast<uint8_t*>(out),
                          reinterpret_cast<uint8_t*>(in), width, height,
                          captured_in_stride);
      };
  }
  return nullptr;
}

}  // namespace image_formats
}  // namespace gfx
}  // namespace scenic
