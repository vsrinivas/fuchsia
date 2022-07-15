// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/simplest_sysmem/png_helper.h"

#include <lib/syslog/cpp/macros.h>
#include <png.h>

namespace png_helper {

void LoadPngFromFile(PNGImageSize* size, uint8_t** out_bytes) {
  FILE* fp = fopen(kSmileyPath, "rb");
  FX_CHECK(fp) << "cannot open file: " << kSmileyPath;

  char header[kPNGHeaderBytes];
  fread(header, 1, kPNGHeaderBytes, fp);

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  FX_CHECK(png) << "png_create_read_struct failed";

  png_infop info = png_create_info_struct(png);
  FX_CHECK(info) << "png_create_info_struct failed";

  png_init_io(png, fp);
  png_set_sig_bytes(png, kPNGHeaderBytes);
  png_read_info(png, info);

  uint32_t width = png_get_image_width(png, info);
  uint32_t height = png_get_image_height(png, info);
  size->width = width;
  size->height = height;

  uint32_t color_type = png_get_color_type(png, info);
  uint32_t bit_depth = png_get_bit_depth(png, info);

  // Only works with 4 bytes (32-bits) per pixel
  FX_CHECK(color_type == PNG_COLOR_TYPE_RGBA) << "currently only supports RGBA";
  FX_CHECK(bit_depth == 8) << "currently only supports 8-bit channel";

  if (setjmp(png_jmpbuf(png))) {
    FX_LOGS(ERROR) << "LoadPngFromFile errored";
  }

  int64_t row_bytes = png_get_rowbytes(png, info);
  int64_t expected_row_bytes = 4UL * width;  // We assume each pixel is 4 bytes.
  FX_CHECK(row_bytes == expected_row_bytes)
      << "unexpected row_bytes: " << row_bytes << " expect: 4 * " << width;

  png_bytep* row_pointers = new png_bytep[height];
  *out_bytes = new uint8_t[row_bytes * height];

  for (uint32_t i = 0; i < height; ++i) {
    row_pointers[i] = reinterpret_cast<png_bytep>(*out_bytes + i * row_bytes);
  }

  png_read_image(png, row_pointers);
  fclose(fp);
  png_destroy_read_struct(&png, &info, NULL);
}

}  // namespace png_helper
