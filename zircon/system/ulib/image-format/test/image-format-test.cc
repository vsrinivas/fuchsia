// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/image-format/image_format.h>
#include <lib/sysmem-version/sysmem-version.h>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace sysmem_v1 = fuchsia_sysmem;
namespace sysmem_v2 = fuchsia_sysmem2;

TEST(ImageFormat, LinearComparison_V2) {
  sysmem_v2::PixelFormat plain;
  plain.type().emplace(sysmem_v2::PixelFormatType::kBgra32);

  sysmem_v2::PixelFormat linear;
  linear.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
  linear.format_modifier_value().emplace(sysmem_v2::kFormatModifierLinear);

  sysmem_v2::PixelFormat x_tiled;
  x_tiled.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
  x_tiled.format_modifier_value().emplace(sysmem_v2::kFormatModifierIntelI915XTiled);

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, plain));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, linear));

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, linear));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, plain));

  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(linear, x_tiled));
  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(plain, x_tiled));
}

TEST(ImageFormat, LinearComparison_V2_wire) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat plain(allocator);
  plain.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);

  sysmem_v2::wire::PixelFormat linear(allocator);
  linear.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  linear.set_format_modifier_value(allocator, sysmem_v2::wire::kFormatModifierLinear);

  sysmem_v2::wire::PixelFormat x_tiled(allocator);
  x_tiled.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  x_tiled.set_format_modifier_value(allocator, sysmem_v2::wire::kFormatModifierIntelI915XTiled);

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, plain));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, linear));

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, linear));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, plain));

  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(linear, x_tiled));
  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(plain, x_tiled));
}

TEST(ImageFormat, LinearComparison_V1_wire) {
  sysmem_v1::wire::PixelFormat plain = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = false,
  };

  sysmem_v1::wire::PixelFormat linear = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierLinear,
          },
  };

  sysmem_v1::wire::PixelFormat x_tiled = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierIntelI915XTiled,
          },
  };

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, plain));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, linear));

  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(plain, linear));
  EXPECT_TRUE(ImageFormatIsPixelFormatEqual(linear, plain));

  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(linear, x_tiled));
  EXPECT_FALSE(ImageFormatIsPixelFormatEqual(plain, x_tiled));
}

TEST(ImageFormat, LinearRowBytes_V2) {
  sysmem_v2::PixelFormat linear;
  linear.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
  linear.format_modifier_value().emplace(sysmem_v2::kFormatModifierLinear);
  sysmem_v2::ImageFormatConstraints constraints;
  constraints.pixel_format().emplace(std::move(linear));
  constraints.min_coded_width().emplace(12u);
  constraints.max_coded_width().emplace(100u);
  constraints.bytes_per_row_divisor().emplace(4u * 8u);
  constraints.max_bytes_per_row().emplace(100000u);

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatMinimumRowBytes(constraints, 17, &row_bytes));
  EXPECT_EQ(row_bytes, 4 * 24);

  EXPECT_FALSE(ImageFormatMinimumRowBytes(constraints, 11, &row_bytes));
  EXPECT_FALSE(ImageFormatMinimumRowBytes(constraints, 101, &row_bytes));
}

TEST(ImageFormat, LinearRowBytes_V2_wire) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat linear(allocator);
  linear.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  linear.set_format_modifier_value(allocator, sysmem_v2::wire::kFormatModifierLinear);
  sysmem_v2::wire::ImageFormatConstraints constraints(allocator);
  constraints.set_pixel_format(allocator, linear);
  constraints.set_min_coded_width(12u);
  constraints.set_max_coded_width(100u);
  constraints.set_bytes_per_row_divisor(4u * 8u);
  constraints.set_max_bytes_per_row(100000u);

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatMinimumRowBytes(constraints, 17, &row_bytes));
  EXPECT_EQ(row_bytes, 4 * 24);

  EXPECT_FALSE(ImageFormatMinimumRowBytes(constraints, 11, &row_bytes));
  EXPECT_FALSE(ImageFormatMinimumRowBytes(constraints, 101, &row_bytes));
}

TEST(ImageFormat, LinearRowBytes_V1_wire) {
  sysmem_v1::wire::PixelFormat linear = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierLinear,
          },
  };
  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = linear,
      .min_coded_width = 12,
      .max_coded_width = 100,
      .max_bytes_per_row = 100000,
      .bytes_per_row_divisor = 4 * 8,
  };

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatMinimumRowBytes(constraints, 17, &row_bytes));
  EXPECT_EQ(row_bytes, 4 * 24);

  EXPECT_FALSE(ImageFormatMinimumRowBytes(constraints, 11, &row_bytes));
  EXPECT_FALSE(ImageFormatMinimumRowBytes(constraints, 101, &row_bytes));
}

TEST(ImageFormat, InvalidColorSpace_V1_wire) {
  fidl::Arena allocator;
  auto sysmem_format_result = ImageFormatConvertZxToSysmem_v1(allocator, ZX_PIXEL_FORMAT_RGB_565);
  EXPECT_TRUE(sysmem_format_result.is_ok());
  auto sysmem_format = sysmem_format_result.take_value();

  sysmem_v1::wire::ColorSpace color_space{sysmem_v1::wire::ColorSpaceType::kInvalid};
  // Shouldn't crash.
  EXPECT_FALSE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, sysmem_format));
}

TEST(ImageFormat, PassThroughColorSpace_V1_wire) {
  fidl::Arena allocator;
  fuchsia_sysmem::wire::PixelFormat linear_bgra = {
      .type = fuchsia_sysmem::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR,
          },
  };

  sysmem_v1::wire::ColorSpace color_space{sysmem_v1::wire::ColorSpaceType::kPassThrough};
  EXPECT_TRUE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, linear_bgra));

  fuchsia_sysmem::wire::PixelFormat linear_nv12 = {
      .type = fuchsia_sysmem::wire::PixelFormatType::kNv12,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR,
          },
  };

  EXPECT_TRUE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, linear_nv12));
}

TEST(ImageFormat, ZxPixelFormat_V2) {
  zx_pixel_format_t pixel_formats[] = {
      ZX_PIXEL_FORMAT_RGB_565,   ZX_PIXEL_FORMAT_RGB_332,  ZX_PIXEL_FORMAT_RGB_2220,
      ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_MONO_8,
      ZX_PIXEL_FORMAT_GRAY_8,    ZX_PIXEL_FORMAT_NV12,     ZX_PIXEL_FORMAT_RGB_888,
  };
  for (zx_pixel_format_t format : pixel_formats) {
    fprintf(stderr, "Format %x\n", format);
    auto sysmem_format_result = ImageFormatConvertZxToSysmem_v2(format);
    EXPECT_TRUE(sysmem_format_result.is_ok());
    sysmem_v2::PixelFormat sysmem_format = sysmem_format_result.take_value();
    zx_pixel_format_t back_format;
    EXPECT_TRUE(ImageFormatConvertSysmemToZx(sysmem_format, &back_format));
    if (format == ZX_PIXEL_FORMAT_RGB_x888) {
      EXPECT_EQ(ZX_PIXEL_FORMAT_ARGB_8888, back_format);
    } else {
      EXPECT_EQ(back_format, format);
    }
    EXPECT_TRUE(sysmem_format.format_modifier_value().has_value());
    EXPECT_EQ(sysmem_v2::kFormatModifierLinear,
              static_cast<uint64_t>(sysmem_format.format_modifier_value().value()));

    sysmem_v2::ColorSpace color_space;
    if (format == ZX_PIXEL_FORMAT_NV12) {
      color_space.type().emplace(sysmem_v2::ColorSpaceType::kRec601Ntsc);
    } else {
      color_space.type().emplace(sysmem_v2::ColorSpaceType::kSrgb);
    }
    EXPECT_TRUE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, sysmem_format));

    EXPECT_EQ(ZX_PIXEL_FORMAT_BYTES(format), ImageFormatStrideBytesPerWidthPixel(sysmem_format));
    EXPECT_TRUE(ImageFormatIsSupported(sysmem_format));
    EXPECT_LT(0u, ImageFormatBitsPerPixel(sysmem_format));
  }

  sysmem_v2::PixelFormat other_format;
  other_format.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
  other_format.format_modifier_value().emplace(sysmem_v2::kFormatModifierIntelI915XTiled);

  zx_pixel_format_t back_format;
  EXPECT_FALSE(ImageFormatConvertSysmemToZx(other_format, &back_format));
  // Treat as linear.
  //
  // clone via generated code
  auto other_format2 = other_format;
  other_format2.format_modifier_value().reset();
  EXPECT_TRUE(ImageFormatConvertSysmemToZx(other_format2, &back_format));
}

TEST(ImageFormat, ZxPixelFormat_V2_wire) {
  fidl::Arena allocator;
  zx_pixel_format_t pixel_formats[] = {
      ZX_PIXEL_FORMAT_RGB_565,   ZX_PIXEL_FORMAT_RGB_332,  ZX_PIXEL_FORMAT_RGB_2220,
      ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_MONO_8,
      ZX_PIXEL_FORMAT_GRAY_8,    ZX_PIXEL_FORMAT_NV12,     ZX_PIXEL_FORMAT_RGB_888,
  };
  for (zx_pixel_format_t format : pixel_formats) {
    fprintf(stderr, "Format %x\n", format);
    auto sysmem_format_result = ImageFormatConvertZxToSysmem_v2(allocator, format);
    EXPECT_TRUE(sysmem_format_result.is_ok());
    sysmem_v2::wire::PixelFormat sysmem_format = sysmem_format_result.take_value();
    zx_pixel_format_t back_format;
    EXPECT_TRUE(ImageFormatConvertSysmemToZx(sysmem_format, &back_format));
    if (format == ZX_PIXEL_FORMAT_RGB_x888) {
      EXPECT_EQ(ZX_PIXEL_FORMAT_ARGB_8888, back_format);
    } else {
      EXPECT_EQ(back_format, format);
    }
    EXPECT_TRUE(sysmem_format.has_format_modifier_value());
    EXPECT_EQ(sysmem_v2::wire::kFormatModifierLinear,
              static_cast<uint64_t>(sysmem_format.format_modifier_value()));

    sysmem_v2::wire::ColorSpace color_space(allocator);
    if (format == ZX_PIXEL_FORMAT_NV12) {
      color_space.set_type(sysmem_v2::wire::ColorSpaceType::kRec601Ntsc);
    } else {
      color_space.set_type(sysmem_v2::wire::ColorSpaceType::kSrgb);
    }
    EXPECT_TRUE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, sysmem_format));

    EXPECT_EQ(ZX_PIXEL_FORMAT_BYTES(format), ImageFormatStrideBytesPerWidthPixel(sysmem_format));
    EXPECT_TRUE(ImageFormatIsSupported(sysmem_format));
    EXPECT_LT(0u, ImageFormatBitsPerPixel(sysmem_format));
  }

  sysmem_v2::wire::PixelFormat other_format(allocator);
  other_format.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  other_format.set_format_modifier_value(allocator,
                                         sysmem_v2::wire::kFormatModifierIntelI915XTiled);

  zx_pixel_format_t back_format;
  EXPECT_FALSE(ImageFormatConvertSysmemToZx(other_format, &back_format));
  // Treat as linear.
  auto other_format2 = sysmem::V2ClonePixelFormat(allocator, other_format);
  other_format2.set_format_modifier_value(nullptr);
  EXPECT_TRUE(ImageFormatConvertSysmemToZx(other_format2, &back_format));
}

TEST(ImageFormat, ZxPixelFormat_V1_wire) {
  fidl::Arena allocator;
  zx_pixel_format_t pixel_formats[] = {
      ZX_PIXEL_FORMAT_RGB_565,   ZX_PIXEL_FORMAT_RGB_332,  ZX_PIXEL_FORMAT_RGB_2220,
      ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_MONO_8,
      ZX_PIXEL_FORMAT_GRAY_8,    ZX_PIXEL_FORMAT_NV12,     ZX_PIXEL_FORMAT_RGB_888,
      ZX_PIXEL_FORMAT_ABGR_8888, ZX_PIXEL_FORMAT_BGR_888x,
  };
  for (zx_pixel_format_t format : pixel_formats) {
    printf("Format %x\n", format);
    auto sysmem_format_result = ImageFormatConvertZxToSysmem_v1(allocator, format);
    EXPECT_TRUE(sysmem_format_result.is_ok());
    auto sysmem_format = sysmem_format_result.take_value();
    zx_pixel_format_t back_format;
    EXPECT_TRUE(ImageFormatConvertSysmemToZx(sysmem_format, &back_format));
    if (format == ZX_PIXEL_FORMAT_RGB_x888) {
      EXPECT_EQ(ZX_PIXEL_FORMAT_ARGB_8888, back_format);
    } else if (format == ZX_PIXEL_FORMAT_BGR_888x) {
      EXPECT_EQ(ZX_PIXEL_FORMAT_ABGR_8888, back_format);
    } else {
      EXPECT_EQ(back_format, format);
    }
    EXPECT_TRUE(sysmem_format.has_format_modifier);
    EXPECT_EQ(fuchsia_sysmem_FORMAT_MODIFIER_LINEAR,
              static_cast<uint64_t>(sysmem_format.format_modifier.value));

    sysmem_v1::wire::ColorSpace color_space;
    if (format == ZX_PIXEL_FORMAT_NV12) {
      color_space.type = sysmem_v1::wire::ColorSpaceType::kRec601Ntsc;
    } else {
      color_space.type = sysmem_v1::wire::ColorSpaceType::kSrgb;
    }
    EXPECT_TRUE(ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, sysmem_format));

    EXPECT_EQ(ZX_PIXEL_FORMAT_BYTES(format), ImageFormatStrideBytesPerWidthPixel(sysmem_format));
    EXPECT_TRUE(ImageFormatIsSupported(sysmem_format));
    EXPECT_LT(0u, ImageFormatBitsPerPixel(sysmem_format));
  }

  sysmem_v1::wire::PixelFormat other_format = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierIntelI915XTiled,
          },
  };

  zx_pixel_format_t back_format;
  EXPECT_FALSE(ImageFormatConvertSysmemToZx(other_format, &back_format));
  // Treat as linear.
  other_format.has_format_modifier = false;
  EXPECT_TRUE(ImageFormatConvertSysmemToZx(other_format, &back_format));
}

TEST(ImageFormat, PlaneByteOffset_V2) {
  sysmem_v2::PixelFormat linear;
  linear.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
  linear.format_modifier_value().emplace(sysmem_v2::kFormatModifierLinear);
  sysmem_v2::ImageFormatConstraints constraints;
  constraints.pixel_format().emplace(std::move(linear));
  constraints.min_coded_width().emplace(12u);
  constraints.max_coded_width().emplace(100u);
  constraints.min_coded_height().emplace(12u);
  constraints.max_coded_height().emplace(100u);
  constraints.bytes_per_row_divisor().emplace(4u * 8u);
  constraints.max_bytes_per_row().emplace(100000u);

  auto image_format_result = ImageConstraintsToFormat(constraints, 18, 17);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();
  // The raw size would be 72 without bytes_per_row_divisor of 32.
  EXPECT_EQ(96u, image_format.bytes_per_row());

  uint64_t byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &byte_offset));
  EXPECT_EQ(0u, byte_offset);
  EXPECT_FALSE(ImageFormatPlaneByteOffset(image_format, 1, &byte_offset));

  // clone via generated code
  auto constraints2 = constraints;
  constraints2.pixel_format()->type().emplace(sysmem_v2::PixelFormatType::kI420);

  constexpr uint32_t kBytesPerRow = 32;
  image_format_result = ImageConstraintsToFormat(constraints2, 18, 20);
  EXPECT_TRUE(image_format_result.is_ok());
  image_format = image_format_result.take_value();
  EXPECT_EQ(kBytesPerRow, image_format.bytes_per_row());
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &byte_offset));
  EXPECT_EQ(0u, byte_offset);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1, &byte_offset));
  EXPECT_EQ(kBytesPerRow * 20, byte_offset);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 2, &byte_offset));
  EXPECT_EQ(kBytesPerRow * 20 + kBytesPerRow / 2 * 20 / 2, byte_offset);
  EXPECT_FALSE(ImageFormatPlaneByteOffset(image_format, 3, &byte_offset));

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &row_bytes));
  EXPECT_EQ(kBytesPerRow, row_bytes);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1, &row_bytes));
  EXPECT_EQ(kBytesPerRow / 2, row_bytes);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 2, &row_bytes));
  EXPECT_EQ(kBytesPerRow / 2, row_bytes);
  EXPECT_FALSE(ImageFormatPlaneRowBytes(image_format, 3, &row_bytes));
}

TEST(ImageFormat, PlaneByteOffset_V2_wire) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat linear(allocator);
  linear.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  linear.set_format_modifier_value(allocator, sysmem_v2::wire::kFormatModifierLinear);
  sysmem_v2::wire::ImageFormatConstraints constraints(allocator);
  constraints.set_pixel_format(allocator, linear);
  constraints.set_min_coded_width(12u);
  constraints.set_max_coded_width(100u);
  constraints.set_min_coded_height(12u);
  constraints.set_max_coded_height(100u);
  constraints.set_bytes_per_row_divisor(4u * 8u);
  constraints.set_max_bytes_per_row(100000u);

  auto image_format_result = ImageConstraintsToFormat(allocator, constraints, 18, 17);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();
  // The raw size would be 72 without bytes_per_row_divisor of 32.
  EXPECT_EQ(96u, image_format.bytes_per_row());

  uint64_t byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &byte_offset));
  EXPECT_EQ(0u, byte_offset);
  EXPECT_FALSE(ImageFormatPlaneByteOffset(image_format, 1, &byte_offset));

  auto constraints2 = sysmem::V2CloneImageFormatConstraints(allocator, constraints);
  constraints2.pixel_format().set_type(sysmem_v2::wire::PixelFormatType::kI420);

  constexpr uint32_t kBytesPerRow = 32;
  image_format_result = ImageConstraintsToFormat(allocator, constraints2, 18, 20);
  EXPECT_TRUE(image_format_result.is_ok());
  image_format = image_format_result.take_value();
  EXPECT_EQ(kBytesPerRow, image_format.bytes_per_row());
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &byte_offset));
  EXPECT_EQ(0u, byte_offset);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1, &byte_offset));
  EXPECT_EQ(kBytesPerRow * 20, byte_offset);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 2, &byte_offset));
  EXPECT_EQ(kBytesPerRow * 20 + kBytesPerRow / 2 * 20 / 2, byte_offset);
  EXPECT_FALSE(ImageFormatPlaneByteOffset(image_format, 3, &byte_offset));

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &row_bytes));
  EXPECT_EQ(kBytesPerRow, row_bytes);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1, &row_bytes));
  EXPECT_EQ(kBytesPerRow / 2, row_bytes);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 2, &row_bytes));
  EXPECT_EQ(kBytesPerRow / 2, row_bytes);
  EXPECT_FALSE(ImageFormatPlaneRowBytes(image_format, 3, &row_bytes));
}

TEST(ImageFormat, PlaneByteOffset_V1_wire) {
  sysmem_v1::wire::PixelFormat linear = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierLinear,
          },
  };
  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = linear,
      .min_coded_width = 12,
      .max_coded_width = 100,
      .min_coded_height = 12,
      .max_coded_height = 100,
      .max_bytes_per_row = 100000,
      .bytes_per_row_divisor = 4 * 8,
  };

  auto image_format_result = ImageConstraintsToFormat(constraints, 18, 17);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();
  // The raw size would be 72 without bytes_per_row_divisor of 32.
  EXPECT_EQ(96u, image_format.bytes_per_row);

  uint64_t byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &byte_offset));
  EXPECT_EQ(0u, byte_offset);
  EXPECT_FALSE(ImageFormatPlaneByteOffset(image_format, 1, &byte_offset));

  constraints.pixel_format.type = sysmem_v1::wire::PixelFormatType::kI420;

  constexpr uint32_t kBytesPerRow = 32;
  image_format_result = ImageConstraintsToFormat(constraints, 18, 20);
  EXPECT_TRUE(image_format_result.is_ok());
  image_format = image_format_result.take_value();
  EXPECT_EQ(kBytesPerRow, image_format.bytes_per_row);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &byte_offset));
  EXPECT_EQ(0u, byte_offset);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1, &byte_offset));
  EXPECT_EQ(kBytesPerRow * 20, byte_offset);
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 2, &byte_offset));
  EXPECT_EQ(kBytesPerRow * 20 + kBytesPerRow / 2 * 20 / 2, byte_offset);
  EXPECT_FALSE(ImageFormatPlaneByteOffset(image_format, 3, &byte_offset));

  uint32_t row_bytes;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &row_bytes));
  EXPECT_EQ(kBytesPerRow, row_bytes);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1, &row_bytes));
  EXPECT_EQ(kBytesPerRow / 2, row_bytes);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 2, &row_bytes));
  EXPECT_EQ(kBytesPerRow / 2, row_bytes);
  EXPECT_FALSE(ImageFormatPlaneRowBytes(image_format, 3, &row_bytes));
}

TEST(ImageFormat, TransactionEliminationFormats_V2) {
  sysmem_v2::PixelFormat format;
  format.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
  format.format_modifier_value().emplace(sysmem_v2::kFormatModifierLinear);

  EXPECT_TRUE(ImageFormatCompatibleWithProtectedMemory(format));

  // clone via generated code
  auto format2 = format;
  format2.format_modifier_value().emplace(sysmem_v2::kFormatModifierArmLinearTe);

  EXPECT_FALSE(ImageFormatCompatibleWithProtectedMemory(format2));

  sysmem_v2::ImageFormatConstraints constraints;
  constraints.pixel_format().emplace(std::move(format2));
  constraints.min_coded_width().emplace(12u);
  constraints.max_coded_width().emplace(100u);
  constraints.min_coded_height().emplace(12u);
  constraints.max_coded_height().emplace(100u);
  constraints.bytes_per_row_divisor().emplace(4u * 8u);
  constraints.max_bytes_per_row().emplace(100000u);

  auto image_format_result = ImageConstraintsToFormat(constraints, 18, 17);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();
  // The raw size would be 72 without bytes_per_row_divisor of 32.
  EXPECT_EQ(96u, image_format.bytes_per_row());

  // Check the color plane data.
  uint32_t row_bytes;
  uint64_t plane_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &plane_offset));
  EXPECT_EQ(0u, plane_offset);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &row_bytes));
  EXPECT_EQ(image_format.bytes_per_row(), row_bytes);

  constexpr uint32_t kTePlane = 3;
  // Check the TE plane data.
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, kTePlane, &plane_offset));
  EXPECT_LE(image_format.bytes_per_row().value() * 17, plane_offset);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, kTePlane, &row_bytes));

  // Row size should be rounded up to 64 bytes.
  EXPECT_EQ(64, row_bytes);
}

TEST(ImageFormat, TransactionEliminationFormats_V2_wire) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat format(allocator);
  format.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  format.set_format_modifier_value(allocator, sysmem_v2::wire::kFormatModifierLinear);

  EXPECT_TRUE(ImageFormatCompatibleWithProtectedMemory(format));

  auto format2 = sysmem::V2ClonePixelFormat(allocator, format);
  format2.set_format_modifier_value(allocator, sysmem_v2::wire::kFormatModifierArmLinearTe);

  EXPECT_FALSE(ImageFormatCompatibleWithProtectedMemory(format2));

  sysmem_v2::wire::ImageFormatConstraints constraints(allocator);
  constraints.set_pixel_format(allocator, format2);
  constraints.set_min_coded_width(12u);
  constraints.set_max_coded_width(100u);
  constraints.set_min_coded_height(12u);
  constraints.set_max_coded_height(100u);
  constraints.set_bytes_per_row_divisor(4u * 8u);
  constraints.set_max_bytes_per_row(100000u);

  auto image_format_result = ImageConstraintsToFormat(allocator, constraints, 18, 17);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();
  // The raw size would be 72 without bytes_per_row_divisor of 32.
  EXPECT_EQ(96u, image_format.bytes_per_row());

  // Check the color plane data.
  uint32_t row_bytes;
  uint64_t plane_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &plane_offset));
  EXPECT_EQ(0u, plane_offset);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &row_bytes));
  EXPECT_EQ(image_format.bytes_per_row(), row_bytes);

  constexpr uint32_t kTePlane = 3;
  // Check the TE plane data.
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, kTePlane, &plane_offset));
  EXPECT_LE(image_format.bytes_per_row() * 17, plane_offset);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, kTePlane, &row_bytes));

  // Row size should be rounded up to 64 bytes.
  EXPECT_EQ(64, row_bytes);
}

TEST(ImageFormat, TransactionEliminationFormats_V1_wire) {
  sysmem_v1::wire::PixelFormat format = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierLinear,
          },
  };
  EXPECT_TRUE(ImageFormatCompatibleWithProtectedMemory(format));

  format.format_modifier.value = sysmem_v1::wire::kFormatModifierArmLinearTe;
  EXPECT_FALSE(ImageFormatCompatibleWithProtectedMemory(format));

  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = format,
      .min_coded_width = 12,
      .max_coded_width = 100,
      .min_coded_height = 12,
      .max_coded_height = 100,
      .max_bytes_per_row = 100000,
      .bytes_per_row_divisor = 4 * 8,
  };

  auto optional_format = ImageConstraintsToFormat(constraints, 18, 17);
  EXPECT_TRUE(optional_format);
  auto& image_format = optional_format.value();
  // The raw size would be 72 without bytes_per_row_divisor of 32.
  EXPECT_EQ(96u, image_format.bytes_per_row);

  // Check the color plane data.
  uint32_t row_bytes;
  uint64_t plane_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0, &plane_offset));
  EXPECT_EQ(0u, plane_offset);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &row_bytes));
  EXPECT_EQ(image_format.bytes_per_row, row_bytes);

  constexpr uint32_t kTePlane = 3;
  // Check the TE plane data.
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, kTePlane, &plane_offset));
  EXPECT_LE(image_format.bytes_per_row * 17, plane_offset);
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, kTePlane, &row_bytes));

  // Row size should be rounded up to 64 bytes.
  EXPECT_EQ(64, row_bytes);
}

TEST(ImageFormat, BasicSizes_V2) {
  constexpr uint32_t kWidth = 64;
  constexpr uint32_t kHeight = 128;
  constexpr uint32_t kStride = kWidth * 6;

  sysmem_v2::ImageFormat image_format_bgra32;
  {
    sysmem_v2::PixelFormat pixel_format;
    pixel_format.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
    image_format_bgra32.pixel_format().emplace(std::move(pixel_format));
  }
  image_format_bgra32.coded_width().emplace(kWidth);
  image_format_bgra32.coded_height().emplace(kHeight);
  image_format_bgra32.bytes_per_row().emplace(kStride);
  EXPECT_EQ(kHeight * kStride, ImageFormatImageSize(image_format_bgra32));
  EXPECT_EQ(1, ImageFormatCodedWidthMinDivisor(image_format_bgra32.pixel_format().value()));
  EXPECT_EQ(1, ImageFormatCodedHeightMinDivisor(image_format_bgra32.pixel_format().value()));
  EXPECT_EQ(4, ImageFormatSampleAlignment(image_format_bgra32.pixel_format().value()));

  sysmem_v2::ImageFormat image_format_nv12;
  {
    sysmem_v2::PixelFormat pixel_format;
    pixel_format.type().emplace(sysmem_v2::PixelFormatType::kNv12);
    image_format_nv12.pixel_format().emplace(std::move(pixel_format));
  }
  image_format_nv12.coded_width().emplace(kWidth);
  image_format_nv12.coded_height().emplace(kHeight);
  image_format_nv12.bytes_per_row().emplace(kStride);
  EXPECT_EQ(kHeight * kStride * 3 / 2, ImageFormatImageSize(image_format_nv12));
  EXPECT_EQ(2, ImageFormatCodedWidthMinDivisor(image_format_nv12.pixel_format().value()));
  EXPECT_EQ(2, ImageFormatCodedHeightMinDivisor(image_format_nv12.pixel_format().value()));
  EXPECT_EQ(2, ImageFormatSampleAlignment(image_format_nv12.pixel_format().value()));
}

TEST(ImageFormat, BasicSizes_V2_wire) {
  fidl::Arena allocator;
  constexpr uint32_t kWidth = 64;
  constexpr uint32_t kHeight = 128;
  constexpr uint32_t kStride = kWidth * 6;

  sysmem_v2::wire::ImageFormat image_format_bgra32(allocator);
  {
    sysmem_v2::wire::PixelFormat pixel_format(allocator);
    pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
    image_format_bgra32.set_pixel_format(allocator, pixel_format);
  }
  image_format_bgra32.set_coded_width(kWidth);
  image_format_bgra32.set_coded_height(kHeight);
  image_format_bgra32.set_bytes_per_row(kStride);
  EXPECT_EQ(kHeight * kStride, ImageFormatImageSize(image_format_bgra32));
  EXPECT_EQ(1, ImageFormatCodedWidthMinDivisor(image_format_bgra32.pixel_format()));
  EXPECT_EQ(1, ImageFormatCodedHeightMinDivisor(image_format_bgra32.pixel_format()));
  EXPECT_EQ(4, ImageFormatSampleAlignment(image_format_bgra32.pixel_format()));

  sysmem_v2::wire::ImageFormat image_format_nv12(allocator);
  {
    sysmem_v2::wire::PixelFormat pixel_format(allocator);
    pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kNv12);
    image_format_nv12.set_pixel_format(allocator, pixel_format);
  }
  image_format_nv12.set_coded_width(kWidth);
  image_format_nv12.set_coded_height(kHeight);
  image_format_nv12.set_bytes_per_row(kStride);
  EXPECT_EQ(kHeight * kStride * 3 / 2, ImageFormatImageSize(image_format_nv12));
  EXPECT_EQ(2, ImageFormatCodedWidthMinDivisor(image_format_nv12.pixel_format()));
  EXPECT_EQ(2, ImageFormatCodedHeightMinDivisor(image_format_nv12.pixel_format()));
  EXPECT_EQ(2, ImageFormatSampleAlignment(image_format_nv12.pixel_format()));
}

TEST(ImageFormat, BasicSizes_V1_wire) {
  constexpr uint32_t kWidth = 64;
  constexpr uint32_t kHeight = 128;
  constexpr uint32_t kStride = 256;

  sysmem_v1::wire::ImageFormat2 image_format_bgra32 = {
      .pixel_format =
          {
              .type = sysmem_v1::wire::PixelFormatType::kBgra32,
          },
      .coded_width = kWidth,
      .coded_height = kHeight,
      .bytes_per_row = kStride,
  };
  EXPECT_EQ(kHeight * kStride, ImageFormatImageSize(image_format_bgra32));
  EXPECT_EQ(1, ImageFormatCodedWidthMinDivisor(image_format_bgra32.pixel_format));
  EXPECT_EQ(1, ImageFormatCodedHeightMinDivisor(image_format_bgra32.pixel_format));
  EXPECT_EQ(4, ImageFormatSampleAlignment(image_format_bgra32.pixel_format));

  sysmem_v1::wire::ImageFormat2 image_format_nv12 = {
      .pixel_format =
          {
              .type = sysmem_v1::wire::PixelFormatType::kNv12,
          },
      .coded_width = kWidth,
      .coded_height = kHeight,
      .bytes_per_row = kStride,
  };
  EXPECT_EQ(kHeight * kStride * 3 / 2, ImageFormatImageSize(image_format_nv12));
  EXPECT_EQ(2, ImageFormatCodedWidthMinDivisor(image_format_nv12.pixel_format));
  EXPECT_EQ(2, ImageFormatCodedHeightMinDivisor(image_format_nv12.pixel_format));
  EXPECT_EQ(2, ImageFormatSampleAlignment(image_format_nv12.pixel_format));
}

TEST(ImageFormat, AfbcFlagFormats_V1_wire) {
  sysmem_v1::wire::PixelFormat format = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierArmAfbc16X16SplitBlockSparseYuvTe,
          },
  };

  EXPECT_FALSE(ImageFormatCompatibleWithProtectedMemory(format));

  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = format,
      .min_coded_width = 12,
      .max_coded_width = 100,
      .min_coded_height = 12,
      .max_coded_height = 100,
      .max_bytes_per_row = 100000,
      .bytes_per_row_divisor = 4 * 8,
  };

  auto optional_format = ImageConstraintsToFormat(constraints, 18, 17);
  EXPECT_TRUE(optional_format);

  sysmem_v1::wire::PixelFormat tiled_format = {
      .type = sysmem_v1::wire::PixelFormatType::kBgra32,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierArmAfbc16X16SplitBlockSparseYuvTiledHeader,
          },
  };

  constraints.pixel_format = tiled_format;

  optional_format = ImageConstraintsToFormat(constraints, 18, 17);
  EXPECT_TRUE(optional_format);
  auto& image_format = optional_format.value();
  constexpr uint32_t kMinHeaderOffset = 4096u;
  constexpr uint32_t kMinWidth = 128;
  constexpr uint32_t kMinHeight = 128;
  EXPECT_EQ(kMinHeaderOffset + kMinWidth * kMinHeight * 4, ImageFormatImageSize(image_format));
}

TEST(ImageFormat, R8G8Formats_V1_wire) {
  sysmem_v1::wire::PixelFormat format = {
      .type = sysmem_v1::wire::PixelFormatType::kR8G8,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierLinear,
          },
  };

  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = format,
      .min_coded_width = 12,
      .max_coded_width = 100,
      .min_coded_height = 12,
      .max_coded_height = 100,
      .max_bytes_per_row = 100000,
      .bytes_per_row_divisor = 1,
  };

  {
    auto optional_format = ImageConstraintsToFormat(constraints, 18, 17);
    EXPECT_TRUE(optional_format);
    auto& image_format = optional_format.value();
    EXPECT_EQ(18u * 2, image_format.bytes_per_row);
    EXPECT_EQ(18u * 17u * 2, ImageFormatImageSize(image_format));
  }

  constraints.pixel_format.type = sysmem_v1::wire::PixelFormatType::kR8;

  {
    auto optional_format = ImageConstraintsToFormat(constraints, 18, 17);
    EXPECT_TRUE(optional_format);
    auto& image_format = optional_format.value();
    EXPECT_EQ(18u * 1, image_format.bytes_per_row);
    EXPECT_EQ(18u * 17u * 1, ImageFormatImageSize(image_format));
  }
}

TEST(ImageFormat, A2R10G10B10_Formats_V1_wire) {
  for (const auto& pixel_format_type : {sysmem_v1::wire::PixelFormatType::kA2R10G10B10,
                                        sysmem_v1::wire::PixelFormatType::kA2B10G10R10}) {
    sysmem_v1::wire::PixelFormat format = {
        .type = pixel_format_type,
        .has_format_modifier = true,
        .format_modifier =
            {
                .value = sysmem_v1::wire::kFormatModifierLinear,
            },
    };

    sysmem_v1::wire::ImageFormatConstraints constraints = {
        .pixel_format = format,
        .min_coded_width = 12,
        .max_coded_width = 100,
        .min_coded_height = 12,
        .max_coded_height = 100,
        .max_bytes_per_row = 100000,
        .bytes_per_row_divisor = 1,
    };

    auto optional_format = ImageConstraintsToFormat(constraints, 18, 17);
    EXPECT_TRUE(optional_format);
    auto& image_format = optional_format.value();
    EXPECT_EQ(18u * 4, image_format.bytes_per_row);
    EXPECT_EQ(18u * 17u * 4, ImageFormatImageSize(image_format));
    EXPECT_EQ(1, ImageFormatCodedWidthMinDivisor(image_format.pixel_format));
    EXPECT_EQ(1, ImageFormatCodedHeightMinDivisor(image_format.pixel_format));
    EXPECT_EQ(4, ImageFormatSampleAlignment(image_format.pixel_format));
  }
}

TEST(ImageFormat, GoldfishOptimal_V2) {
  constexpr uint32_t kWidth = 64;
  constexpr uint32_t kHeight = 128;
  constexpr uint32_t kStride = kWidth * 6;

  sysmem_v2::ImageFormat linear_image_format_bgra32;
  {
    sysmem_v2::PixelFormat pixel_format;
    pixel_format.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
    linear_image_format_bgra32.pixel_format().emplace(std::move(pixel_format));
  }
  linear_image_format_bgra32.coded_width().emplace(kWidth);
  linear_image_format_bgra32.coded_height().emplace(kHeight);
  linear_image_format_bgra32.bytes_per_row().emplace(kStride);

  sysmem_v2::ImageFormat goldfish_optimal_image_format_bgra32;
  {
    sysmem_v2::PixelFormat pixel_format;
    pixel_format.type().emplace(sysmem_v2::PixelFormatType::kBgra32);
    pixel_format.format_modifier_value().emplace(sysmem_v2::kFormatModifierGoogleGoldfishOptimal);
    goldfish_optimal_image_format_bgra32.pixel_format().emplace(std::move(pixel_format));
  }
  goldfish_optimal_image_format_bgra32.coded_width().emplace(kWidth);
  goldfish_optimal_image_format_bgra32.coded_height().emplace(kHeight);
  goldfish_optimal_image_format_bgra32.bytes_per_row().emplace(kStride);
  EXPECT_EQ(ImageFormatImageSize(linear_image_format_bgra32),
            ImageFormatImageSize(goldfish_optimal_image_format_bgra32));
  EXPECT_EQ(
      ImageFormatCodedWidthMinDivisor(linear_image_format_bgra32.pixel_format().value()),
      ImageFormatCodedWidthMinDivisor(goldfish_optimal_image_format_bgra32.pixel_format().value()));
  EXPECT_EQ(ImageFormatCodedHeightMinDivisor(linear_image_format_bgra32.pixel_format().value()),
            ImageFormatCodedHeightMinDivisor(
                goldfish_optimal_image_format_bgra32.pixel_format().value()));
  EXPECT_EQ(
      ImageFormatSampleAlignment(linear_image_format_bgra32.pixel_format().value()),
      ImageFormatSampleAlignment(goldfish_optimal_image_format_bgra32.pixel_format().value()));
}

TEST(ImageFormat, GoldfishOptimal_V2_wire) {
  fidl::Arena allocator;
  constexpr uint32_t kWidth = 64;
  constexpr uint32_t kHeight = 128;
  constexpr uint32_t kStride = kWidth * 6;

  sysmem_v2::wire::ImageFormat linear_image_format_bgra32(allocator);
  {
    sysmem_v2::wire::PixelFormat pixel_format(allocator);
    pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
    linear_image_format_bgra32.set_pixel_format(allocator, pixel_format);
  }
  linear_image_format_bgra32.set_coded_width(kWidth);
  linear_image_format_bgra32.set_coded_height(kHeight);
  linear_image_format_bgra32.set_bytes_per_row(kStride);

  sysmem_v2::wire::ImageFormat goldfish_optimal_image_format_bgra32(allocator);
  {
    sysmem_v2::wire::PixelFormat pixel_format(allocator);
    pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
    pixel_format.set_format_modifier_value(allocator,
                                           sysmem_v2::wire::kFormatModifierGoogleGoldfishOptimal);
    goldfish_optimal_image_format_bgra32.set_pixel_format(allocator, pixel_format);
  }
  goldfish_optimal_image_format_bgra32.set_coded_width(kWidth);
  goldfish_optimal_image_format_bgra32.set_coded_height(kHeight);
  goldfish_optimal_image_format_bgra32.set_bytes_per_row(kStride);
  EXPECT_EQ(ImageFormatImageSize(linear_image_format_bgra32),
            ImageFormatImageSize(goldfish_optimal_image_format_bgra32));
  EXPECT_EQ(ImageFormatCodedWidthMinDivisor(linear_image_format_bgra32.pixel_format()),
            ImageFormatCodedWidthMinDivisor(goldfish_optimal_image_format_bgra32.pixel_format()));
  EXPECT_EQ(ImageFormatCodedHeightMinDivisor(linear_image_format_bgra32.pixel_format()),
            ImageFormatCodedHeightMinDivisor(goldfish_optimal_image_format_bgra32.pixel_format()));
  EXPECT_EQ(ImageFormatSampleAlignment(linear_image_format_bgra32.pixel_format()),
            ImageFormatSampleAlignment(goldfish_optimal_image_format_bgra32.pixel_format()));
}

TEST(ImageFormat, CorrectModifiers) {
  EXPECT_EQ(sysmem_v1::kFormatModifierArmAfbc16X16YuvTiledHeader,
            sysmem_v1::kFormatModifierArmAfbc16X16YuvTiledHeader);
  EXPECT_EQ(sysmem_v1::kFormatModifierArmAfbc16X16YuvTiledHeader,
            sysmem_v1::kFormatModifierArmAfbc16X16 | sysmem_v1::kFormatModifierArmYuvBit |
                sysmem_v1::kFormatModifierArmTiledHeaderBit);
  EXPECT_EQ(sysmem_v1::kFormatModifierGoogleGoldfishOptimal,
            sysmem_v2::kFormatModifierGoogleGoldfishOptimal);
}

TEST(ImageFormat, CorrectModifiers_wire) {
  EXPECT_EQ(sysmem_v1::wire::kFormatModifierArmAfbc16X16YuvTiledHeader,
            sysmem_v1::wire::kFormatModifierArmAfbc16X16YuvTiledHeader);
  EXPECT_EQ(sysmem_v1::wire::kFormatModifierArmAfbc16X16YuvTiledHeader,
            sysmem_v1::wire::kFormatModifierArmAfbc16X16 |
                sysmem_v1::wire::kFormatModifierArmYuvBit |
                sysmem_v1::wire::kFormatModifierArmTiledHeaderBit);
  EXPECT_EQ(sysmem_v1::wire::kFormatModifierGoogleGoldfishOptimal,
            sysmem_v2::wire::kFormatModifierGoogleGoldfishOptimal);
}

TEST(ImageFormat, IntelYTiledFormat_V2) {
  sysmem_v2::PixelFormat pixel_format;
  pixel_format.type().emplace(sysmem_v2::PixelFormatType::kNv12);
  pixel_format.format_modifier_value().emplace(sysmem_v2::kFormatModifierIntelI915YTiled);
  sysmem_v2::ImageFormatConstraints constraints;
  constraints.pixel_format().emplace(std::move(pixel_format));
  constraints.min_coded_width().emplace(128u);
  constraints.min_coded_height().emplace(32u);
  constraints.bytes_per_row_divisor().emplace(0u);
  constraints.max_bytes_per_row().emplace(0u);

  auto image_format_result = ImageConstraintsToFormat(constraints, 3440u, 1440u);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();

  constexpr uint32_t kTileSize = 4096u;
  constexpr uint32_t kBytesPerRowPerTile = 128u;

  constexpr uint32_t kYPlaneWidthInTiles = 27u;
  constexpr uint32_t kYPlaneHeightInTiles = 45u;
  constexpr uint32_t kUVPlaneWidthInTiles = 27u;
  constexpr uint32_t kUVPlaneHeightInTiles = 23u;

  constexpr uint32_t kYPlaneSize = kYPlaneWidthInTiles * kYPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kUVPlaneSize = kUVPlaneWidthInTiles * kUVPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kTotalSize = kYPlaneSize + kUVPlaneSize;

  EXPECT_EQ(kTotalSize, ImageFormatImageSize(image_format));

  uint64_t y_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0u, &y_plane_byte_offset));
  EXPECT_EQ(0u, y_plane_byte_offset);

  uint64_t uv_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1u, &uv_plane_byte_offset));
  EXPECT_EQ(kYPlaneSize, uv_plane_byte_offset);

  uint32_t y_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0u, &y_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kYPlaneWidthInTiles, y_plane_row_stride);

  uint32_t uv_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1u, &uv_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kUVPlaneWidthInTiles, uv_plane_row_stride);
}

TEST(ImageFormat, IntelYTiledFormat_V2_wire) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat pixel_format(allocator);
  pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kNv12);
  pixel_format.set_format_modifier_value(allocator,
                                         sysmem_v2::wire::kFormatModifierIntelI915YTiled);
  sysmem_v2::wire::ImageFormatConstraints constraints(allocator);
  constraints.set_pixel_format(allocator, pixel_format);
  constraints.set_min_coded_width(128u);
  constraints.set_min_coded_height(32u);
  constraints.set_bytes_per_row_divisor(0u);
  constraints.set_max_bytes_per_row(0u);

  auto image_format_result = ImageConstraintsToFormat(allocator, constraints, 3440u, 1440u);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();

  constexpr uint32_t kTileSize = 4096u;
  constexpr uint32_t kBytesPerRowPerTile = 128u;

  constexpr uint32_t kYPlaneWidthInTiles = 27u;
  constexpr uint32_t kYPlaneHeightInTiles = 45u;
  constexpr uint32_t kUVPlaneWidthInTiles = 27u;
  constexpr uint32_t kUVPlaneHeightInTiles = 23u;

  constexpr uint32_t kYPlaneSize = kYPlaneWidthInTiles * kYPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kUVPlaneSize = kUVPlaneWidthInTiles * kUVPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kTotalSize = kYPlaneSize + kUVPlaneSize;

  EXPECT_EQ(kTotalSize, ImageFormatImageSize(image_format));

  uint64_t y_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0u, &y_plane_byte_offset));
  EXPECT_EQ(0u, y_plane_byte_offset);

  uint64_t uv_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1u, &uv_plane_byte_offset));
  EXPECT_EQ(kYPlaneSize, uv_plane_byte_offset);

  uint32_t y_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0u, &y_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kYPlaneWidthInTiles, y_plane_row_stride);

  uint32_t uv_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1u, &uv_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kUVPlaneWidthInTiles, uv_plane_row_stride);
}

TEST(ImageFormat, IntelYTiledFormat_V1_wire) {
  sysmem_v1::wire::PixelFormat format = {
      .type = sysmem_v1::wire::PixelFormatType::kNv12,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierIntelI915YTiled,
          },
  };

  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = format,
      .min_coded_width = 128u,
      .max_coded_width = 1920u,
      .min_coded_height = 32u,
      .max_coded_height = 1080u,
      .max_bytes_per_row = 0u,
      .bytes_per_row_divisor = 0u,
  };

  auto optional_format = ImageConstraintsToFormat(constraints, 1920u, 1080u);
  EXPECT_TRUE(optional_format);
  auto& image_format = optional_format.value();

  constexpr uint32_t kTileSize = 4096u;
  constexpr uint32_t kBytesPerRowPerTile = 128u;

  constexpr uint32_t kYPlaneWidthInTiles = 15u;
  constexpr uint32_t kYPlaneHeightInTiles = 34u;
  constexpr uint32_t kUVPlaneWidthInTiles = 15u;
  constexpr uint32_t kUVPlaneHeightInTiles = 17u;

  constexpr uint32_t kYPlaneSize = kYPlaneWidthInTiles * kYPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kUVPlaneSize = kUVPlaneWidthInTiles * kUVPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kTotalSize = kYPlaneSize + kUVPlaneSize;

  EXPECT_EQ(kTotalSize, ImageFormatImageSize(image_format));

  uint64_t y_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0u, &y_plane_byte_offset));
  EXPECT_EQ(0u, y_plane_byte_offset);

  uint64_t uv_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1u, &uv_plane_byte_offset));
  EXPECT_EQ(kYPlaneSize, uv_plane_byte_offset);

  uint32_t y_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0u, &y_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kYPlaneWidthInTiles, y_plane_row_stride);

  uint32_t uv_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1u, &uv_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kUVPlaneWidthInTiles, uv_plane_row_stride);
}

TEST(ImageFormat, IntelCcsFormats_V1_wire) {
  for (auto& format_modifier : {
           sysmem_v1::wire::kFormatModifierIntelI915YTiledCcs,
           sysmem_v1::wire::kFormatModifierIntelI915YfTiledCcs,
       }) {
    sysmem_v1::wire::PixelFormat format = {
        .type = sysmem_v1::wire::PixelFormatType::kBgra32,
        .has_format_modifier = true,
        .format_modifier =
            {
                .value = format_modifier,
            },
    };

    sysmem_v1::wire::ImageFormatConstraints constraints = {
        .pixel_format = format,
        .min_coded_width = 12,
        .max_coded_width = 100,
        .min_coded_height = 12,
        .max_coded_height = 100,
        .max_bytes_per_row = 100000,
        .bytes_per_row_divisor = 4 * 8,
    };

    auto optional_format = ImageConstraintsToFormat(constraints, 64, 63);
    EXPECT_TRUE(optional_format);
    auto& image_format = optional_format.value();

    constexpr uint32_t kWidthInTiles = 2;
    constexpr uint32_t kHeightInTiles = 2;
    constexpr uint32_t kTileSize = 4096;
    constexpr uint32_t kMainPlaneSize = kWidthInTiles * kHeightInTiles * kTileSize;
    constexpr uint32_t kCcsWidthInTiles = 1;
    constexpr uint32_t kCcsHeightInTiles = 1;
    constexpr uint32_t kCcsPlane = 3;
    EXPECT_EQ(kMainPlaneSize + kCcsWidthInTiles * kCcsHeightInTiles * kTileSize,
              ImageFormatImageSize(image_format));
    uint64_t ccs_byte_offset;
    EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, kCcsPlane, &ccs_byte_offset));
    EXPECT_EQ(kMainPlaneSize, ccs_byte_offset);

    uint32_t main_plane_row_stride;
    EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &main_plane_row_stride));
    EXPECT_EQ(128u * kWidthInTiles, main_plane_row_stride);
    uint32_t ccs_row_stride;
    EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, kCcsPlane, &ccs_row_stride));
    EXPECT_EQ(ccs_row_stride, 128u * kCcsWidthInTiles);
  }
}
