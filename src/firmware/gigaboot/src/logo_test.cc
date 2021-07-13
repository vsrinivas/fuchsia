// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logo.h"

#include <vector>

#include <efi/protocol/graphics-output.h>
#include <gtest/gtest.h>

namespace {

constexpr efi_graphics_output_blt_pixel kGarbagePixel{
    .Blue = 200,
    .Green = 30,
    .Red = 2,
    .Reserved = 75,
};

void ExpectPixel(const efi_graphics_output_blt_pixel& pixel, int r, int g, int b) {
  EXPECT_EQ(pixel.Red, r);
  EXPECT_EQ(pixel.Green, g);
  EXPECT_EQ(pixel.Blue, b);
  // This is important, non-zero Reserved can cause visual artifacts.
  EXPECT_EQ(pixel.Reserved, 0);
}

TEST(LogoTest, LogoLoad) {
  static_assert(262144 == logo_height * logo_width, "Update test to match logo");

  std::vector<efi_graphics_output_blt_pixel> pixels(logo_height * logo_width, kGarbagePixel);
  logo_load(pixels.data());

  // Output from image_to_rle.py:
  // INFO: Test index 0 = 0, RGB = [0, 0, 0]
  // INFO: Test index 60140 = 64, RGB = [60, 60, 61]
  // INFO: Test index 90547 = 96, RGB = [90, 91, 91]
  // INFO: Test index 91905 = 255, RGB = [241, 243, 244]
  // INFO: Test index 179305 = 0, RGB = [0, 0, 0]
  // INFO: Test index 262143 = 0, RGB = [0, 0, 0]
  ExpectPixel(pixels[0], 0, 0, 0);
  ExpectPixel(pixels[60140], 60, 60, 61);
  ExpectPixel(pixels[90547], 90, 91, 91);
  ExpectPixel(pixels[91905], 241, 243, 244);
  ExpectPixel(pixels[179305], 0, 0, 0);
  ExpectPixel(pixels[262143], 0, 0, 0);
}

}  // namespace
