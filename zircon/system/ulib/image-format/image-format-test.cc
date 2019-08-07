// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/image-format/image_format.h>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

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
