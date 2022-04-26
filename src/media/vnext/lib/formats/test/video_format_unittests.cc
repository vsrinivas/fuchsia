// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/media/vnext/lib/formats/video_format.h"

namespace fmlib {
namespace {

constexpr fuchsia::math::Size kZeroSize{.width = 0, .height = 0};
constexpr fuchsia::mediastreams::PixelFormat kInvalidPixelFormat =
    fuchsia::mediastreams::PixelFormat::INVALID;
constexpr fuchsia::mediastreams::ColorSpace kInvalidColorSpace =
    fuchsia::mediastreams::ColorSpace::INVALID;

// Tests the |sysmem_pixel_format| method.
TEST(VideoFormatTests, SysmemPixelFormat) {
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::R8G8B8A8, kInvalidColorSpace,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::R8G8B8A8, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::BGRA32, kInvalidColorSpace,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::BGRA32, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::I420, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::I420, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::M420, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::M420, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::NV12, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::YUY2, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::YUY2, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::MJPEG, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::MJPEG, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::YV12, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::YV12, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::BGR24, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::BGR24, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::RGB565, kInvalidColorSpace,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::RGB565, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::RGB332, kInvalidColorSpace,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::RGB332, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::RGB2220, kInvalidColorSpace,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::RGB2220, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::L8, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::L8, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::R8, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::R8, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::R8G8, kInvalidColorSpace, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::R8G8, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
  {
    VideoFormat under_test(fuchsia::mediastreams::PixelFormat::INVALID, kInvalidColorSpace,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::INVALID, under_test.sysmem_pixel_format().type);
    EXPECT_FALSE(under_test.sysmem_pixel_format().has_format_modifier);
  }
}

// Tests the |sysmem_color_space| method.
TEST(VideoFormatTests, SysmemColorSpace) {
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::SRGB, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::SRGB, under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::REC601_NTSC,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC601_NTSC, under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat,
                           fuchsia::mediastreams::ColorSpace::REC601_NTSC_FULL_RANGE, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE,
              under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::REC601_PAL,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC601_PAL, under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat,
                           fuchsia::mediastreams::ColorSpace::REC601_PAL_FULL_RANGE, kZeroSize,
                           kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE,
              under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::REC709,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC709, under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::REC2020,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC2020, under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::REC2100,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC2100, under_test.sysmem_color_space().type);
  }
  {
    VideoFormat under_test(kInvalidPixelFormat, fuchsia::mediastreams::ColorSpace::INVALID,
                           kZeroSize, kZeroSize, nullptr, nullptr, nullptr);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::INVALID, under_test.sysmem_color_space().type);
  }
}

}  // namespace
}  // namespace fmlib
