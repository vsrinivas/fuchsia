// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/image-format/image_format.h>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "fuchsia/sysmem/c/fidl.h"

TEST(ImageFormat, LinearComparison) {
  fuchsia_sysmem_PixelFormat plain = {};
  plain.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  plain.has_format_modifier = false;

  fuchsia_sysmem_PixelFormat linear = {};
  linear.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  linear.has_format_modifier = true;
  linear.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;

  fuchsia_sysmem_PixelFormat x_tiled = {};
  x_tiled.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  x_tiled.has_format_modifier = true;
  x_tiled.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED;

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, plain));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, linear));

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, linear));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, plain));

  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(linear, x_tiled));
  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(plain, x_tiled));
}

TEST(ImageFormat, LinearRowBytes) {
  fuchsia_sysmem_PixelFormat linear = {};
  linear.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  linear.has_format_modifier = true;
  linear.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
  fuchsia_sysmem_ImageFormatConstraints constraints = {};
  constraints.pixel_format = linear;
  constraints.min_coded_width = 12;
  constraints.max_coded_width = 100;
  constraints.bytes_per_row_divisor = 4 * 8;
  constraints.max_bytes_per_row = 100000;

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatMinimumRowBytes(&constraints, 17, &row_bytes));
  EXPECT_EQ(row_bytes, 4 * 24);

  EXPECT_FALSE(ImageFormatMinimumRowBytes(&constraints, 11, &row_bytes));
  EXPECT_FALSE(ImageFormatMinimumRowBytes(&constraints, 101, &row_bytes));
}

TEST(ImageFormat, ZxPixelFormat) {
  zx_pixel_format_t pixel_formats[] = {
      ZX_PIXEL_FORMAT_RGB_565,   ZX_PIXEL_FORMAT_RGB_332,  ZX_PIXEL_FORMAT_RGB_2220,
      ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_MONO_8,
      ZX_PIXEL_FORMAT_GRAY_8,    ZX_PIXEL_FORMAT_NV12,     ZX_PIXEL_FORMAT_RGB_888,
  };
  for (zx_pixel_format_t format : pixel_formats) {
    fprintf(stderr, "Format %x\n", format);
    fuchsia_sysmem_PixelFormat sysmem_format;
    EXPECT_TRUE(ImageFormatConvertZxToSysmem(format, &sysmem_format));
    zx_pixel_format_t back_format;
    EXPECT_TRUE(ImageFormatConvertSysmemToZx(&sysmem_format, &back_format));
    if (format == ZX_PIXEL_FORMAT_RGB_x888) {
      EXPECT_EQ(ZX_PIXEL_FORMAT_ARGB_8888, back_format);
    } else {
      EXPECT_EQ(back_format, format);
    }
    EXPECT_TRUE(sysmem_format.has_format_modifier);
    EXPECT_EQ(fuchsia_sysmem_FORMAT_MODIFIER_LINEAR,
              static_cast<uint64_t>(sysmem_format.format_modifier.value));

    fuchsia_sysmem_ColorSpace color_space;
    if (format == ZX_PIXEL_FORMAT_NV12) {
      color_space.type = fuchsia_sysmem_ColorSpaceType_REC601_NTSC;
    } else {
      color_space.type = fuchsia_sysmem_ColorSpaceType_SRGB;
    }
    EXPECT_TRUE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, sysmem_format));

    EXPECT_EQ(ZX_PIXEL_FORMAT_BYTES(format), ImageFormatStrideBytesPerWidthPixel(&sysmem_format));
    EXPECT_TRUE(ImageFormatIsSupported(&sysmem_format));
    EXPECT_LT(0u, ImageFormatBitsPerPixel(&sysmem_format));
  }

  fuchsia_sysmem_PixelFormat other_format;
  other_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  other_format.has_format_modifier = true;
  other_format.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED;

  zx_pixel_format_t back_format;
  EXPECT_FALSE(ImageFormatConvertSysmemToZx(&other_format, &back_format));
  // Treat as linear.
  other_format.has_format_modifier = false;
  EXPECT_TRUE(ImageFormatConvertSysmemToZx(&other_format, &back_format));
}
