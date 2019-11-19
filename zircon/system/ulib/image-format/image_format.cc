// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/image-format/image_format.h"

#include <zircon/assert.h>

#include <map>
#include <set>

#include <fbl/algorithm.h>

#include "fuchsia/sysmem/c/fidl.h"

namespace {

// There are two aspects of the ColorSpace and PixelFormat that we care about:
//   * bits-per-sample - bits per primary sample (R, G, B, or Y)
//   * RGB vs. YUV - whether the system supports the ColorSpace or PixelFormat
//     representing RGB data or YUV data.  Any given ColorSpace only supports
//     one or the other. Currently any given PixelFormat only supports one or
//     the other and this isn't likely to change.
// While we could just list all the ColorSpace(s) that each PixelFormat could
// plausibly support, expressing in terms of bits-per-sample and RGB vs. YUV is
// perhaps easier to grok.

enum ColorType { kColorType_NONE, kColorType_RGB, kColorType_YUV };

struct SamplingInfo {
  std::set<uint32_t> possible_bits_per_sample;
  ColorType color_type;
};

const std::map<fuchsia_sysmem_ColorSpaceType, SamplingInfo> kColorSpaceSamplingInfo = {
    {fuchsia_sysmem_ColorSpaceType_SRGB, {{8, 10, 12, 16}, kColorType_RGB}},
    {fuchsia_sysmem_ColorSpaceType_REC601_NTSC, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC601_NTSC_FULL_RANGE, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC601_PAL, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC601_PAL_FULL_RANGE, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC709, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC2020, {{10, 12}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC2100, {{10, 12}, kColorType_YUV}},
};
const std::map<fuchsia_sysmem_PixelFormatType, SamplingInfo> kPixelFormatSamplingInfo = {
    {fuchsia_sysmem_PixelFormatType_R8G8B8A8, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_BGRA32, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_I420, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_M420, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_NV12, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_YUY2, {{8}, kColorType_YUV}},
    // 8 bits RGB when uncompressed - in this context, MJPEG is essentially
    // pretending to be uncompressed.
    {fuchsia_sysmem_PixelFormatType_MJPEG, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_YV12, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_BGR24, {{8}, kColorType_RGB}},

    // These use the same colorspaces as regular 8-bit-per-component formats
    {fuchsia_sysmem_PixelFormatType_RGB565, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_RGB332, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_RGB2220, {{8}, kColorType_RGB}},
    // Expands to RGB
    {fuchsia_sysmem_PixelFormatType_L8, {{8}, kColorType_RGB}},
};

class ImageFormatSet {
 public:
  virtual bool IsSupported(const fuchsia_sysmem_PixelFormat* pixel_format) const = 0;
  virtual uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format) const = 0;
};

class IntelTiledFormats : public ImageFormatSet {
 public:
  bool IsSupported(const fuchsia_sysmem_PixelFormat* pixel_format) const override {
    if (!pixel_format->has_format_modifier)
      return false;
    if (pixel_format->type != fuchsia_sysmem_PixelFormatType_R8G8B8A8 &&
        pixel_format->type != fuchsia_sysmem_PixelFormatType_BGRA32) {
      return false;
    }
    switch (pixel_format->format_modifier.value) {
      case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED:
      case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED:
      case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED:
        return true;
      default:
        return false;
    }
  }
  uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format) const override {
    // See
    // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-skl-vol05-memory_views.pdf
    constexpr uint32_t kIntelTileByteSize = 4096;
    constexpr uint32_t kIntelYTilePixelWidth = 32;
    constexpr uint32_t kIntelYTileHeight = 4096 / (kIntelYTilePixelWidth * 4);
    constexpr uint32_t kIntelXTilePixelWidth = 128;
    constexpr uint32_t kIntelXTileHeight = 4096 / (kIntelXTilePixelWidth * 4);
    constexpr uint32_t kIntelYFTilePixelWidth = 32;  // For a 4 byte per component format
    constexpr uint32_t kIntelYFTileHeight = 4096 / (kIntelYFTilePixelWidth * 4);
    ZX_DEBUG_ASSERT(IsSupported(&image_format->pixel_format));
    switch (image_format->pixel_format.format_modifier.value) {
      case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED:
        return fbl::round_up(image_format->coded_width, kIntelXTilePixelWidth) /
               kIntelXTilePixelWidth *
               fbl::round_up(image_format->coded_height, kIntelXTileHeight) / kIntelXTileHeight *
               kIntelTileByteSize;

      case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED:
        return fbl::round_up(image_format->coded_width, kIntelYTilePixelWidth) /
               kIntelYTilePixelWidth *
               fbl::round_up(image_format->coded_height, kIntelYTileHeight) / kIntelYTileHeight *
               kIntelTileByteSize;

      case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED:
        return fbl::round_up(image_format->coded_width, kIntelYFTilePixelWidth) /
               kIntelYFTilePixelWidth *
               fbl::round_up(image_format->coded_height, kIntelYFTileHeight) / kIntelYFTileHeight *
               kIntelTileByteSize;
      default:
        return 0u;
    }
  }
};

class AfbcFormats : public ImageFormatSet {
 public:
  bool IsSupported(const fuchsia_sysmem_PixelFormat* pixel_format) const override {
    if (!pixel_format->has_format_modifier)
      return false;
    if (pixel_format->type != fuchsia_sysmem_PixelFormatType_R8G8B8A8 &&
        pixel_format->type != fuchsia_sysmem_PixelFormatType_BGRA32) {
      return false;
    }
    switch (pixel_format->format_modifier.value) {
      case fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_16x16:
      case fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32x8:
        return true;
      default:
        return false;
    }
  }
  uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format) const override {
    // See
    // https://android.googlesource.com/device/linaro/hikey/+/android-o-preview-3/gralloc960/alloc_device.cpp
    constexpr uint32_t kAfbcBodyAlignment = 1024u;

    ZX_DEBUG_ASSERT(IsSupported(&image_format->pixel_format));
    uint32_t block_width;
    uint32_t block_height;
    switch (image_format->pixel_format.format_modifier.value) {
      case fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_16x16:
        block_width = 16;
        block_height = 16;
        break;

      case fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32x8:
        block_width = 32;
        block_height = 8;
        break;
      default:
        return 0;
    }

    ZX_DEBUG_ASSERT(image_format->pixel_format.type == fuchsia_sysmem_PixelFormatType_R8G8B8A8 ||
                    image_format->pixel_format.type == fuchsia_sysmem_PixelFormatType_BGRA32);
    constexpr uint32_t kBytesPerPixel = 4;
    constexpr uint32_t kBytesPerBlockHeader = 16;

    uint64_t block_count = fbl::round_up(image_format->coded_width, block_width) / block_width *
                           fbl::round_up(image_format->coded_height, block_height) / block_height;
    return block_count * block_width * block_height * kBytesPerPixel +
           fbl::round_up(block_count * kBytesPerBlockHeader, kAfbcBodyAlignment);
  }
};

class LinearFormats : public ImageFormatSet {
  bool IsSupported(const fuchsia_sysmem_PixelFormat* pixel_format) const override {
    if (pixel_format->has_format_modifier &&
        pixel_format->format_modifier.value != fuchsia_sysmem_FORMAT_MODIFIER_LINEAR) {
      return false;
    }
    switch (pixel_format->type) {
      case fuchsia_sysmem_PixelFormatType_INVALID:
      case fuchsia_sysmem_PixelFormatType_MJPEG:
        return false;
      case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      case fuchsia_sysmem_PixelFormatType_BGRA32:
      case fuchsia_sysmem_PixelFormatType_BGR24:
      case fuchsia_sysmem_PixelFormatType_I420:
      case fuchsia_sysmem_PixelFormatType_M420:
      case fuchsia_sysmem_PixelFormatType_NV12:
      case fuchsia_sysmem_PixelFormatType_YUY2:
      case fuchsia_sysmem_PixelFormatType_YV12:
      case fuchsia_sysmem_PixelFormatType_RGB565:
      case fuchsia_sysmem_PixelFormatType_RGB332:
      case fuchsia_sysmem_PixelFormatType_RGB2220:
      case fuchsia_sysmem_PixelFormatType_L8:
        return true;
    }
    return false;
  }
  uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format) const override {
    ZX_DEBUG_ASSERT(IsSupported(&image_format->pixel_format));

    uint64_t coded_height = image_format->coded_height;
    uint64_t bytes_per_row = image_format->bytes_per_row;
    switch (image_format->pixel_format.type) {
      case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      case fuchsia_sysmem_PixelFormatType_BGRA32:
      case fuchsia_sysmem_PixelFormatType_BGR24:
      case fuchsia_sysmem_PixelFormatType_RGB565:
      case fuchsia_sysmem_PixelFormatType_RGB332:
      case fuchsia_sysmem_PixelFormatType_RGB2220:
      case fuchsia_sysmem_PixelFormatType_L8:
        return coded_height * bytes_per_row;
      case fuchsia_sysmem_PixelFormatType_I420:
        return coded_height * bytes_per_row * 3 / 2;
      case fuchsia_sysmem_PixelFormatType_M420:
        return coded_height * bytes_per_row * 3 / 2;
      case fuchsia_sysmem_PixelFormatType_NV12:
        return coded_height * bytes_per_row * 3 / 2;
      case fuchsia_sysmem_PixelFormatType_YUY2:
        return coded_height * bytes_per_row;
      case fuchsia_sysmem_PixelFormatType_YV12:
        return coded_height * bytes_per_row * 3 / 2;
      default:
        return 0u;
    }
  }
};

constexpr LinearFormats kLinearFormats;
constexpr IntelTiledFormats kIntelFormats;
constexpr AfbcFormats kAfbcFormats;

constexpr const ImageFormatSet* kImageFormats[] = {
    &kLinearFormats,
    &kIntelFormats,
    &kAfbcFormats,
};

}  // namespace

bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem_PixelFormat& a,
                                   const fuchsia_sysmem_PixelFormat& b) {
  if (a.type != b.type)
    return false;
  uint64_t format_modifier_a =
      a.has_format_modifier ? a.format_modifier.value : fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
  uint64_t format_modifier_b =
      b.has_format_modifier ? b.format_modifier.value : fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
  return format_modifier_a == format_modifier_b;
}

bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const fuchsia_sysmem_ColorSpace& color_space, const fuchsia_sysmem_PixelFormat& pixel_format) {
  // Ignore pixel format modifier - assume it has already been checked.
  auto color_space_sampling_info_iter = kColorSpaceSamplingInfo.find(color_space.type);
  if (color_space_sampling_info_iter == kColorSpaceSamplingInfo.end()) {
    return false;
  }
  auto pixel_format_sampling_info_iter = kPixelFormatSamplingInfo.find(pixel_format.type);
  if (pixel_format_sampling_info_iter == kPixelFormatSamplingInfo.end()) {
    return false;
  }
  const SamplingInfo& color_space_sampling_info = color_space_sampling_info_iter->second;
  const SamplingInfo& pixel_format_sampling_info = pixel_format_sampling_info_iter->second;
  if (color_space_sampling_info.color_type != pixel_format_sampling_info.color_type) {
    return false;
  }
  bool is_bits_per_sample_match_found = false;
  for (uint32_t bits_per_sample : color_space_sampling_info.possible_bits_per_sample) {
    auto pixel_format_bits_per_sample_iter =
        pixel_format_sampling_info.possible_bits_per_sample.find(bits_per_sample);
    if (pixel_format_bits_per_sample_iter !=
        pixel_format_sampling_info.possible_bits_per_sample.end()) {
      is_bits_per_sample_match_found = true;
      break;
    }
  }
  if (!is_bits_per_sample_match_found) {
    return false;
  }
  return true;
}

bool ImageFormatIsSupported(const fuchsia_sysmem_PixelFormat* pixel_format) {
  for (auto& format_set : kImageFormats) {
    if (format_set->IsSupported(pixel_format))
      return true;
  }
  return false;
}

// Overall bits per pixel, across all pixel data in the whole image.
uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format->type) {
    case fuchsia_sysmem_PixelFormatType_INVALID:
    case fuchsia_sysmem_PixelFormatType_MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return 4u * 8u;
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      return 4u * 8u;
    case fuchsia_sysmem_PixelFormatType_BGR24:
      return 3u * 8u;
    case fuchsia_sysmem_PixelFormatType_I420:
      return 12u;
    case fuchsia_sysmem_PixelFormatType_M420:
      return 12u;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return 12u;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return 2u * 8u;
    case fuchsia_sysmem_PixelFormatType_YV12:
      return 12u;
    case fuchsia_sysmem_PixelFormatType_RGB565:
      return 16u;
    case fuchsia_sysmem_PixelFormatType_RGB332:
    case fuchsia_sysmem_PixelFormatType_RGB2220:
    case fuchsia_sysmem_PixelFormatType_L8:
      return 8u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
  return 0u;
}

uint32_t ImageFormatStrideBytesPerWidthPixel(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  // This list should match the one in garnet/public/rust/fuchsia-framebuffer/src/sysmem.rs.
  switch (pixel_format->type) {
    case fuchsia_sysmem_PixelFormatType_INVALID:
    case fuchsia_sysmem_PixelFormatType_MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return 4u;
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      return 4u;
    case fuchsia_sysmem_PixelFormatType_BGR24:
      return 3u;
    case fuchsia_sysmem_PixelFormatType_I420:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_M420:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YV12:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB565:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_RGB332:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB2220:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
  return 0u;
}

uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format) {
  for (auto& format_set : kImageFormats) {
    if (format_set->IsSupported(&image_format->pixel_format))
      return format_set->ImageFormatImageSize(image_format);
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(image_format->pixel_format.type));
  return 0;
}

uint32_t ImageFormatCodedWidthMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format->type) {
    case fuchsia_sysmem_PixelFormatType_INVALID:
    case fuchsia_sysmem_PixelFormatType_MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_BGR24:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_I420:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_M420:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YV12:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_RGB565:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB332:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB2220:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
  return 0u;
}

uint32_t ImageFormatCodedHeightMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format->type) {
    case fuchsia_sysmem_PixelFormatType_INVALID:
    case fuchsia_sysmem_PixelFormatType_MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_BGR24:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_I420:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_M420:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YV12:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_RGB565:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB332:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB2220:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
  return 0u;
}

uint32_t ImageFormatSampleAlignment(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format->type) {
    case fuchsia_sysmem_PixelFormatType_INVALID:
    case fuchsia_sysmem_PixelFormatType_MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return 4u;
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      return 4u;
    case fuchsia_sysmem_PixelFormatType_BGR24:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_I420:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_M420:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_YV12:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_RGB565:
      return 2u;
    case fuchsia_sysmem_PixelFormatType_RGB332:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_RGB2220:
      return 1u;
    case fuchsia_sysmem_PixelFormatType_L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
  return 0u;
}

bool ImageFormatMinimumRowBytes(const fuchsia_sysmem_ImageFormatConstraints* constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out) {
  // Bytes per row is not well-defined for tiled types.
  ZX_DEBUG_ASSERT(!constraints->pixel_format.has_format_modifier ||
                  constraints->pixel_format.format_modifier.value ==
                      fuchsia_sysmem_FORMAT_MODIFIER_LINEAR);
  if (width < constraints->min_coded_width || width > constraints->max_coded_width) {
    return false;
  }
  // This code should match the code in garnet/public/rust/fuchsia-framebuffer/src/sysmem.rs.
  *minimum_row_bytes_out = fbl::round_up(
      fbl::max(ImageFormatStrideBytesPerWidthPixel(&constraints->pixel_format) * width,
               constraints->min_bytes_per_row),
      constraints->bytes_per_row_divisor);
  ZX_ASSERT(*minimum_row_bytes_out <= constraints->max_bytes_per_row);
  return true;
}

bool ImageFormatConvertSysmemToZx(const fuchsia_sysmem_PixelFormat* pixel_format,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  if (pixel_format->has_format_modifier &&
      (pixel_format->format_modifier.value != fuchsia_sysmem_FORMAT_MODIFIER_LINEAR)) {
    return false;
  }
  switch (pixel_format->type) {
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_ARGB_8888;
      return true;

    case fuchsia_sysmem_PixelFormatType_BGR24:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_888;
      return true;

    case fuchsia_sysmem_PixelFormatType_RGB565:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_565;
      return true;

    case fuchsia_sysmem_PixelFormatType_RGB332:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_332;
      return true;

    case fuchsia_sysmem_PixelFormatType_RGB2220:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_2220;
      return true;

    case fuchsia_sysmem_PixelFormatType_L8:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_MONO_8;
      return true;

    case fuchsia_sysmem_PixelFormatType_NV12:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_NV12;
      return true;

    default:
      return false;
  }
}

bool ImageFormatConvertZxToSysmem(zx_pixel_format_t zx_pixel_format,
                                  fuchsia_sysmem_PixelFormat* pixel_format_out) {
  pixel_format_out->has_format_modifier = true;
  pixel_format_out->format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
  switch (zx_pixel_format) {
    case ZX_PIXEL_FORMAT_RGB_565:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_RGB565;
      return true;

    case ZX_PIXEL_FORMAT_RGB_332:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_RGB332;
      return true;

    case ZX_PIXEL_FORMAT_RGB_2220:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_RGB2220;
      return true;

    case ZX_PIXEL_FORMAT_ARGB_8888:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_BGRA32;
      return true;

    case ZX_PIXEL_FORMAT_RGB_x888:
      // Switch to using alpha.
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_BGRA32;
      return true;

    case ZX_PIXEL_FORMAT_MONO_8:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_L8;
      return true;

    case ZX_PIXEL_FORMAT_NV12:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_NV12;
      return true;

    case ZX_PIXEL_FORMAT_RGB_888:
      pixel_format_out->type = fuchsia_sysmem_PixelFormatType_BGR24;
      return true;

    default:
      return false;
  }
}
