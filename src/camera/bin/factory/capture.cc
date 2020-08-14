// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/capture.h"

#include <lib/syslog/cpp/macros.h>
#include <png.h>

namespace camera {

fit::result<std::unique_ptr<Capture>, zx_status_t> Capture::Create(uint32_t stream,
                                                                   const std::string path,
                                                                   bool want_image,
                                                                   CaptureResponse callback) {
  auto capture = std::unique_ptr<Capture>(new Capture);
  capture->stream_ = stream;
  capture->want_image_ = want_image;
  capture->image_ = std::make_unique<std::basic_string<uint8_t>>();
  capture->callback_ = std::move(callback);
  return fit::ok(std::move(capture));
}

Capture::Capture() {}

void Capture::WritePNGAsNV12(FILE* fp) {
  auto& iformat = properties_.image_format;
  auto& pformat = iformat.pixel_format;
  FX_LOGS(INFO) << "writing format " << static_cast<int>(pformat.type) << " as NV12";

  // NV12 is 8 bit Y (width x height) then 8 bit UV (width x height/2)

  uint32_t width = iformat.coded_width;
  uint32_t height = iformat.coded_height;
  auto ypos = image_->data();
  auto uvpos = ypos + iformat.bytes_per_row * height;

  auto png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  auto info_ptr = png_create_info_struct(png_ptr);
  if (setjmp(png_jmpbuf(png_ptr))) {
    FX_LOGS(ERROR) << "libpng failed";
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return;
  }
  png_init_io(png_ptr, fp);
  png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);

  for (uint32_t i = 0; i < height; i++) {
    png_byte row[width * 3];
    for (uint32_t j = 0; j < width; j++) {
      int32_t y = ypos[j];
      int32_t u = uvpos[static_cast<int32_t>(j / 2) * 2] - 128;
      int32_t v = uvpos[static_cast<int32_t>(j / 2) * 2 + 1] - 128;
#define CLIP(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))
      row[j * 3 + 0] = CLIP(y + 1.28033 * v);
      row[j * 3 + 1] = CLIP(y - 0.21482 * u - 0.38059 * v);
      row[j * 3 + 2] = CLIP(y + 2.12798 * u);
    }
    png_write_row(png_ptr, row);
    ypos += iformat.bytes_per_row;
    if (i % 2) {
      uvpos += iformat.bytes_per_row;
    }
  }
  png_write_end(png_ptr, NULL);
  png_destroy_write_struct(&png_ptr, &info_ptr);

  fflush(fp);
  // caller will fclose(fp)
  return;
}

// write image as grayscale png.
// If isBayer, output the top 2/3 (Y of YUV NV12 format) and use 16-bit gray
void Capture::WritePNGUnprocessed(FILE* fp, bool isBayer) {
  auto& iformat = properties_.image_format;
  auto& pformat = iformat.pixel_format;
  FX_LOGS(INFO) << "writing format " << static_cast<int>(pformat.type) << " as unprocessed gray";

  uint32_t vmo_size = image_->size();
  uint32_t width = iformat.bytes_per_row;              // pretend it's 8-bit gray
  uint32_t height = vmo_size / iformat.bytes_per_row;  // number of whole rows, could be non-image
  uint32_t depth = 8;
  uint32_t scanline = iformat.bytes_per_row;

  auto png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  auto info_ptr = png_create_info_struct(png_ptr);
  uint8_t* pos = image_->data();
  if (setjmp(png_jmpbuf(png_ptr))) {
    FX_LOGS(ERROR) << "libpng failed";
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return;
  }
  // TODO(fxbug.dev/58283) remove when stream format is accurate for bayer mode
  if (isBayer) {
    // 2200x2720 is from sensor config
    width = 2200;
    height = 2720 - 40;         // -40 determined experimentally
    depth = 16;
    scanline = 2208;            // pass to a multiple of 16 pixels?
  }
  png_init_io(png_ptr, fp);
  png_set_IHDR(png_ptr, info_ptr, width, height, depth, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);

  std::vector<uint8_t> row;     // only used for 16-bit case
  row.resize(width*2);
  for (uint32_t i = 0; i < height; i++) {
    if (depth == 8) {
      png_write_row(png_ptr, pos);  // consumes width bytes
    } else {
      for (uint32_t j = 0; j < width; j++) {
        row[j*2] = pos[j];      // MSB goes here
        row[j*2+1] = 0;         // LSB is 0
      }
      png_write_row(png_ptr, row.data());
    }
    pos += scanline;
  }
  png_write_end(png_ptr, NULL);
  png_destroy_write_struct(&png_ptr, &info_ptr);

  fflush(fp);
  // caller will fclose(fp)
  return;
}

}  // namespace camera
