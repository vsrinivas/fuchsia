// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/image-format/image_format.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/sysmem-make-tracking/make_tracking.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <zircon/assert.h>

#include <algorithm>
#include <map>
#include <set>

#include <fbl/algorithm.h>

namespace {

using ColorSpace = llcpp::fuchsia::sysmem2::ColorSpace;
using ColorSpaceType = llcpp::fuchsia::sysmem2::ColorSpaceType;
using ImageFormat = llcpp::fuchsia::sysmem2::ImageFormat;
using ImageFormatConstraints = llcpp::fuchsia::sysmem2::ImageFormatConstraints;
using PixelFormat = llcpp::fuchsia::sysmem2::PixelFormat;
using PixelFormatType = llcpp::fuchsia::sysmem2::PixelFormatType;

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

const std::map<ColorSpaceType, SamplingInfo> kColorSpaceSamplingInfo = {
    {ColorSpaceType::SRGB, {{8, 10, 12, 16}, kColorType_RGB}},
    {ColorSpaceType::REC601_NTSC, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::REC601_NTSC_FULL_RANGE, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::REC601_PAL, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::REC601_PAL_FULL_RANGE, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::REC709, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::REC2020, {{10, 12}, kColorType_YUV}},
    {ColorSpaceType::REC2100, {{10, 12}, kColorType_YUV}},
};
const std::map<PixelFormatType, SamplingInfo> kPixelFormatSamplingInfo = {
    {PixelFormatType::R8G8B8A8, {{8}, kColorType_RGB}},
    {PixelFormatType::BGRA32, {{8}, kColorType_RGB}},
    {PixelFormatType::I420, {{8}, kColorType_YUV}},
    {PixelFormatType::M420, {{8}, kColorType_YUV}},
    {PixelFormatType::NV12, {{8}, kColorType_YUV}},
    {PixelFormatType::YUY2, {{8}, kColorType_YUV}},
    // 8 bits RGB when uncompressed - in this context, MJPEG is essentially
    // pretending to be uncompressed.
    {PixelFormatType::MJPEG, {{8}, kColorType_RGB}},
    {PixelFormatType::YV12, {{8}, kColorType_YUV}},
    {PixelFormatType::BGR24, {{8}, kColorType_RGB}},

    // These use the same colorspaces as regular 8-bit-per-component formats
    {PixelFormatType::RGB565, {{8}, kColorType_RGB}},
    {PixelFormatType::RGB332, {{8}, kColorType_RGB}},
    {PixelFormatType::RGB2220, {{8}, kColorType_RGB}},
    // Expands to RGB
    {PixelFormatType::L8, {{8}, kColorType_RGB}},
};

constexpr uint32_t kTransactionEliminationAlignment = 64;
// The transaction elimination buffer is always reported as plane 3.
constexpr uint32_t kTransactionEliminationPlane = 3;

static uint64_t arm_transaction_elimination_row_size(uint32_t width) {
  uint32_t kTileSize = 32;
  uint32_t kBytesPerTilePerRow = 16;
  uint32_t width_in_tiles = fbl::round_up(width, kTileSize) / kTileSize;
  return fbl::round_up(width_in_tiles * kBytesPerTilePerRow, kTransactionEliminationAlignment);
}

static uint64_t arm_transaction_elimination_buffer_size(uint64_t start, uint32_t width,
                                                        uint32_t height) {
  uint32_t kTileSize = 32;
  uint32_t end = start;
  end = fbl::round_up(end, kTransactionEliminationAlignment);
  uint32_t kHeaderSize = kTransactionEliminationAlignment;
  end += kHeaderSize;
  uint32_t height_in_tiles = fbl::round_up(height, kTileSize) / kTileSize;
  end += arm_transaction_elimination_row_size(width) * 2 * height_in_tiles;
  return end - start;
}

class ImageFormatSet {
 public:
  virtual const char* Name() const = 0;
  virtual bool IsSupported(const PixelFormat& pixel_format) const = 0;
  virtual uint64_t ImageFormatImageSize(const ImageFormat& image_format) const = 0;
  virtual bool ImageFormatPlaneByteOffset(const ImageFormat& image_format, uint32_t plane,
                                          uint64_t* offset_out) const = 0;
  virtual bool ImageFormatPlaneRowBytes(const ImageFormat& image_format, uint32_t plane,
                                        uint32_t* row_bytes_out) const = 0;
};

class IntelTiledFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "IntelTiledFormats"; }

  bool IsSupported(const PixelFormat& pixel_format) const override {
    if (!pixel_format.has_type())
      return false;
    if (!pixel_format.has_format_modifier_value())
      return false;
    if (pixel_format.type() != PixelFormatType::R8G8B8A8 &&
        pixel_format.type() != PixelFormatType::BGRA32) {
      return false;
    }
    switch (pixel_format.format_modifier_value()) {
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_X_TILED:
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_Y_TILED:
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_YF_TILED:
        return true;
      default:
        return false;
    }
  }
  uint64_t ImageFormatImageSize(const ImageFormat& image_format) const override {
    // See
    // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-skl-vol05-memory_views.pdf
    constexpr uint32_t kIntelTileByteSize = 4096;
    constexpr uint32_t kIntelYTilePixelWidth = 32;
    constexpr uint32_t kIntelYTileHeight = 4096 / (kIntelYTilePixelWidth * 4);
    constexpr uint32_t kIntelXTilePixelWidth = 128;
    constexpr uint32_t kIntelXTileHeight = 4096 / (kIntelXTilePixelWidth * 4);
    constexpr uint32_t kIntelYFTilePixelWidth = 32;  // For a 4 byte per component format
    constexpr uint32_t kIntelYFTileHeight = 4096 / (kIntelYFTilePixelWidth * 4);
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));
    switch (image_format.pixel_format().format_modifier_value()) {
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_X_TILED:
        return fbl::round_up(image_format.coded_width(), kIntelXTilePixelWidth) /
               kIntelXTilePixelWidth *
               fbl::round_up(image_format.coded_height(), kIntelXTileHeight) / kIntelXTileHeight *
               kIntelTileByteSize;

      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_Y_TILED:
        return fbl::round_up(image_format.coded_width(), kIntelYTilePixelWidth) /
               kIntelYTilePixelWidth *
               fbl::round_up(image_format.coded_height(), kIntelYTileHeight) / kIntelYTileHeight *
               kIntelTileByteSize;

      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_YF_TILED:
        return fbl::round_up(image_format.coded_width(), kIntelYFTilePixelWidth) /
               kIntelYFTilePixelWidth *
               fbl::round_up(image_format.coded_height(), kIntelYFTileHeight) / kIntelYFTileHeight *
               kIntelTileByteSize;
      default:
        return 0u;
    }
  }
  bool ImageFormatPlaneByteOffset(const ImageFormat& image_format, uint32_t plane,
                                  uint64_t* offset_out) const override {
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));
    if (plane == 0) {
      *offset_out = 0;
      return true;
    }
    return false;
  }
  bool ImageFormatPlaneRowBytes(const ImageFormat& image_format, uint32_t plane,
                                uint32_t* row_bytes_out) const override {
    if (plane == 0) {
      *row_bytes_out = 0;
      return true;
    } else {
      return false;
    }
  }
};

class AfbcFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "AfbcFormats"; }

  static constexpr uint64_t kAfbcModifierMask =
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_TE_BIT |
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_SPLIT_BLOCK_BIT |
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_SPARSE_BIT |
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_YUV_BIT |
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_BCH_BIT |
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_TILED_HEADER_BIT;
  bool IsSupported(const PixelFormat& pixel_format) const override {
    if (!pixel_format.has_format_modifier_value())
      return false;
    if (!pixel_format.has_type())
      return false;
    if (pixel_format.type() != PixelFormatType::R8G8B8A8 &&
        pixel_format.type() != PixelFormatType::BGRA32) {
      return false;
    }
    switch (pixel_format.format_modifier_value() & ~kAfbcModifierMask) {
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16:
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8:
        return true;
      default:
        return false;
    }
  }

  // Calculate the size of the Raw AFBC image without a transaction elimination buffer.
  uint64_t NonTESize(const ImageFormat& image_format) const {
    // See
    // https://android.googlesource.com/device/linaro/hikey/+/android-o-preview-3/gralloc960/alloc_device.cpp
    constexpr uint32_t kAfbcBodyAlignment = 1024u;
    constexpr uint32_t kTiledAfbcBodyAlignment = 4096u;

    ZX_DEBUG_ASSERT(image_format.has_pixel_format());
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));
    uint32_t block_width;
    uint32_t block_height;
    uint32_t width_alignment;
    uint32_t height_alignment;
    bool tiled_header = image_format.pixel_format().format_modifier_value() &
                        llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_TILED_HEADER_BIT;

    switch (image_format.pixel_format().format_modifier_value() & ~kAfbcModifierMask) {
      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16:
        block_width = 16;
        block_height = 16;
        if (!tiled_header) {
          width_alignment = block_width;
          height_alignment = block_height;
        } else {
          width_alignment = 128;
          height_alignment = 128;
        }
        break;

      case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8:
        block_width = 32;
        block_height = 8;
        if (!tiled_header) {
          width_alignment = block_width;
          height_alignment = block_height;
        } else {
          width_alignment = 256;
          height_alignment = 64;
        }
        break;
      default:
        return 0;
    }

    uint32_t body_alignment = tiled_header ? kTiledAfbcBodyAlignment : kAfbcBodyAlignment;

    ZX_DEBUG_ASSERT(image_format.pixel_format().has_type());
    ZX_DEBUG_ASSERT(image_format.pixel_format().type() == PixelFormatType::R8G8B8A8 ||
                    image_format.pixel_format().type() == PixelFormatType::BGRA32);
    constexpr uint32_t kBytesPerPixel = 4;
    constexpr uint32_t kBytesPerBlockHeader = 16;

    ZX_DEBUG_ASSERT(image_format.has_coded_width());
    ZX_DEBUG_ASSERT(image_format.has_coded_height());
    uint64_t block_count =
        fbl::round_up(image_format.coded_width(), width_alignment) / block_width *
        fbl::round_up(image_format.coded_height(), height_alignment) / block_height;
    return block_count * block_width * block_height * kBytesPerPixel +
           fbl::round_up(block_count * kBytesPerBlockHeader, body_alignment);
  }

  uint64_t ImageFormatImageSize(const ImageFormat& image_format) const override {
    uint64_t size = NonTESize(image_format);
    if (image_format.pixel_format().format_modifier_value() &
        llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_TE_BIT) {
      size += arm_transaction_elimination_buffer_size(size, image_format.coded_width(),
                                                      image_format.coded_height());
    }

    return size;
  }

  bool ImageFormatPlaneByteOffset(const ImageFormat& image_format, uint32_t plane,
                                  uint64_t* offset_out) const override {
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));
    if (plane == 0) {
      *offset_out = 0;
      return true;
    } else if (plane == kTransactionEliminationPlane) {
      *offset_out = fbl::round_up(NonTESize(image_format), kTransactionEliminationAlignment);
      return true;
    }
    return false;
  }
  bool ImageFormatPlaneRowBytes(const ImageFormat& image_format, uint32_t plane,
                                uint32_t* row_bytes_out) const override {
    if (plane == 0) {
      *row_bytes_out = 0;
      return true;
    } else if (plane == kTransactionEliminationPlane) {
      *row_bytes_out = arm_transaction_elimination_row_size(image_format.coded_width());
      return true;
    }
    return false;
  }
};

static uint64_t linear_size(uint32_t coded_height, uint32_t bytes_per_row, PixelFormatType type) {
  switch (type) {
    case PixelFormatType::R8G8B8A8:
    case PixelFormatType::BGRA32:
    case PixelFormatType::BGR24:
    case PixelFormatType::RGB565:
    case PixelFormatType::RGB332:
    case PixelFormatType::RGB2220:
    case PixelFormatType::L8:
      return coded_height * bytes_per_row;
    case PixelFormatType::I420:
      return coded_height * bytes_per_row * 3 / 2;
    case PixelFormatType::M420:
      return coded_height * bytes_per_row * 3 / 2;
    case PixelFormatType::NV12:
      return coded_height * bytes_per_row * 3 / 2;
    case PixelFormatType::YUY2:
      return coded_height * bytes_per_row;
    case PixelFormatType::YV12:
      return coded_height * bytes_per_row * 3 / 2;
    default:
      return 0u;
  }
}

class LinearFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "LinearFormats"; }

  bool IsSupported(const PixelFormat& pixel_format) const override {
    if (pixel_format.has_format_modifier_value() &&
        pixel_format.format_modifier_value() != llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_LINEAR) {
      return false;
    }
    ZX_DEBUG_ASSERT(pixel_format.has_type());
    switch (pixel_format.type()) {
      case PixelFormatType::INVALID:
      case PixelFormatType::MJPEG:
        return false;
      case PixelFormatType::R8G8B8A8:
      case PixelFormatType::BGRA32:
      case PixelFormatType::BGR24:
      case PixelFormatType::I420:
      case PixelFormatType::M420:
      case PixelFormatType::NV12:
      case PixelFormatType::YUY2:
      case PixelFormatType::YV12:
      case PixelFormatType::RGB565:
      case PixelFormatType::RGB332:
      case PixelFormatType::RGB2220:
      case PixelFormatType::L8:
        return true;
    }
    return false;
  }

  uint64_t ImageFormatImageSize(const ImageFormat& image_format) const override {
    ZX_DEBUG_ASSERT(image_format.has_pixel_format());
    ZX_DEBUG_ASSERT(image_format.pixel_format().has_type());
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));
    ZX_DEBUG_ASSERT(image_format.has_coded_height());
    ZX_DEBUG_ASSERT(image_format.has_bytes_per_row());
    uint32_t coded_height = image_format.has_coded_height() ? image_format.coded_height() : 0;
    uint32_t bytes_per_row = image_format.has_bytes_per_row() ? image_format.bytes_per_row() : 0;
    return linear_size(coded_height, bytes_per_row, image_format.pixel_format().type());
  }

  bool ImageFormatPlaneByteOffset(const ImageFormat& image_format, uint32_t plane,
                                  uint64_t* offset_out) const override {
    if (plane == 0) {
      *offset_out = 0;
      return true;
    }
    if (plane == 1) {
      switch (image_format.pixel_format().type()) {
        case llcpp::fuchsia::sysmem2::PixelFormatType::NV12:
        case llcpp::fuchsia::sysmem2::PixelFormatType::I420:
        case llcpp::fuchsia::sysmem2::PixelFormatType::YV12:
          *offset_out = image_format.coded_height() * image_format.bytes_per_row();
          return true;
        default:
          return false;
      }
    }
    if (plane == 2) {
      switch (image_format.pixel_format().type()) {
        case llcpp::fuchsia::sysmem2::PixelFormatType::I420:
        case llcpp::fuchsia::sysmem2::PixelFormatType::YV12:
          *offset_out = image_format.coded_height() * image_format.bytes_per_row();
          *offset_out += image_format.coded_height() / 2 * image_format.bytes_per_row() / 2;
          return true;
        default:
          return false;
      }
    }

    return false;
  }

  bool ImageFormatPlaneRowBytes(const ImageFormat& image_format, uint32_t plane,
                                uint32_t* row_bytes_out) const override {
    if (plane == 0) {
      *row_bytes_out = image_format.bytes_per_row();
      return true;
    } else if (plane == 1) {
      switch (image_format.pixel_format().type()) {
        case llcpp::fuchsia::sysmem2::PixelFormatType::NV12:
          *row_bytes_out = image_format.bytes_per_row();
          return true;
        case llcpp::fuchsia::sysmem2::PixelFormatType::I420:
        case llcpp::fuchsia::sysmem2::PixelFormatType::YV12:
          *row_bytes_out = image_format.bytes_per_row() / 2;
          return true;
        default:
          return false;
      }
    } else if (plane == 2) {
      switch (image_format.pixel_format().type()) {
        case llcpp::fuchsia::sysmem2::PixelFormatType::I420:
        case llcpp::fuchsia::sysmem2::PixelFormatType::YV12:
          *row_bytes_out = image_format.bytes_per_row() / 2;
          return true;
        default:
          return false;
      }
    }
    return false;
  }
};

constexpr LinearFormats kLinearFormats;

class ArmTELinearFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "ArmTELinearFormats"; }

  bool IsSupported(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) const override {
    if (!pixel_format.has_format_modifier_value()) {
      return false;
    }
    if (pixel_format.format_modifier_value() !=
        llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_LINEAR_TE)
      return false;
    switch (pixel_format.type()) {
      case llcpp::fuchsia::sysmem2::PixelFormatType::INVALID:
      case llcpp::fuchsia::sysmem2::PixelFormatType::MJPEG:
        return false;
      case llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8:
      case llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32:
      case llcpp::fuchsia::sysmem2::PixelFormatType::BGR24:
      case llcpp::fuchsia::sysmem2::PixelFormatType::I420:
      case llcpp::fuchsia::sysmem2::PixelFormatType::M420:
      case llcpp::fuchsia::sysmem2::PixelFormatType::NV12:
      case llcpp::fuchsia::sysmem2::PixelFormatType::YUY2:
      case llcpp::fuchsia::sysmem2::PixelFormatType::YV12:
      case llcpp::fuchsia::sysmem2::PixelFormatType::RGB565:
      case llcpp::fuchsia::sysmem2::PixelFormatType::RGB332:
      case llcpp::fuchsia::sysmem2::PixelFormatType::RGB2220:
      case llcpp::fuchsia::sysmem2::PixelFormatType::L8:
        return true;
    }
    return false;
  }

  uint64_t ImageFormatImageSize(
      const llcpp::fuchsia::sysmem2::ImageFormat& image_format) const override {
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));
    ZX_DEBUG_ASSERT(image_format.has_coded_width());
    ZX_DEBUG_ASSERT(image_format.has_coded_height());
    ZX_DEBUG_ASSERT(image_format.has_bytes_per_row());
    uint32_t coded_width = image_format.has_coded_width() ? image_format.coded_width() : 0;
    uint32_t coded_height = image_format.has_coded_height() ? image_format.coded_height() : 0;
    uint32_t bytes_per_row = image_format.has_bytes_per_row() ? image_format.bytes_per_row() : 0;
    uint64_t size = linear_size(coded_height, bytes_per_row, image_format.pixel_format().type());
    uint64_t crc_size = arm_transaction_elimination_buffer_size(size, coded_width, coded_height);
    return size + crc_size;
  }

  bool ImageFormatPlaneByteOffset(const llcpp::fuchsia::sysmem2::ImageFormat& image_format,

                                  uint32_t plane, uint64_t* offset_out) const override {
    if (plane < kTransactionEliminationPlane) {
      return kLinearFormats.ImageFormatPlaneByteOffset(image_format, plane, offset_out);
    } else if (plane == kTransactionEliminationPlane) {
      ZX_DEBUG_ASSERT(image_format.has_coded_height());
      ZX_DEBUG_ASSERT(image_format.has_bytes_per_row());
      uint32_t coded_height = image_format.has_coded_height() ? image_format.coded_height() : 0;
      uint32_t bytes_per_row = image_format.has_bytes_per_row() ? image_format.bytes_per_row() : 0;
      uint64_t size = linear_size(coded_height, bytes_per_row, image_format.pixel_format().type());
      *offset_out = fbl::round_up(size, 64u);
      return true;
    }

    return false;
  }

  bool ImageFormatPlaneRowBytes(const llcpp::fuchsia::sysmem2::ImageFormat& image_format,
                                uint32_t plane, uint32_t* row_bytes_out) const override {
    if (plane < kTransactionEliminationPlane) {
      return kLinearFormats.ImageFormatPlaneRowBytes(image_format, plane, row_bytes_out);
    } else if (plane == kTransactionEliminationPlane) {
      *row_bytes_out = arm_transaction_elimination_row_size(image_format.coded_width());
      return true;
    }
    return false;
  }
};

constexpr IntelTiledFormats kIntelFormats;
constexpr AfbcFormats kAfbcFormats;
constexpr ArmTELinearFormats kArmTELinearFormats;

constexpr const ImageFormatSet* kImageFormats[] = {
    &kLinearFormats,
    &kIntelFormats,
    &kAfbcFormats,
    &kArmTELinearFormats,
};

}  // namespace

bool ImageFormatIsPixelFormatEqual(const llcpp::fuchsia::sysmem2::PixelFormat& a,
                                   const llcpp::fuchsia::sysmem2::PixelFormat& b) {
  if (a.type() != b.type()) {
    return false;
  }
  uint64_t format_modifier_a = a.has_format_modifier_value()
                                   ? a.format_modifier_value()
                                   : llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_NONE;
  uint64_t format_modifier_b = b.has_format_modifier_value()
                                   ? b.format_modifier_value()
                                   : llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_NONE;
  if (format_modifier_a != format_modifier_b) {
    return false;
  }
  return true;
}

bool ImageFormatIsPixelFormatEqual(const llcpp::fuchsia::sysmem::PixelFormat& a_v1,
                                   const llcpp::fuchsia::sysmem::PixelFormat& b_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat a = sysmem::V2CopyFromV1PixelFormat(&allocator, a_v1).build();
  PixelFormat b = sysmem::V2CopyFromV1PixelFormat(&allocator, b_v1).build();
  return ImageFormatIsPixelFormatEqual(a, b);
}

bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem_PixelFormat& a_v1,
                                   const fuchsia_sysmem_PixelFormat& b_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat a = sysmem::V2CopyFromV1PixelFormat(&allocator, a_v1).build();
  PixelFormat b = sysmem::V2CopyFromV1PixelFormat(&allocator, b_v1).build();
  return ImageFormatIsPixelFormatEqual(a, b);
}

bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const llcpp::fuchsia::sysmem2::ColorSpace& color_space,
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  if (!color_space.has_type())
    return false;
  // Ignore pixel format modifier - assume it has already been checked.
  auto color_space_sampling_info_iter = kColorSpaceSamplingInfo.find(color_space.type());
  if (color_space_sampling_info_iter == kColorSpaceSamplingInfo.end()) {
    return false;
  }
  auto pixel_format_sampling_info_iter = kPixelFormatSamplingInfo.find(pixel_format.type());
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

bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const llcpp::fuchsia::sysmem::ColorSpace& color_space_v1,
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  ColorSpace color_space = sysmem::V2CopyFromV1ColorSpace(&allocator, color_space_v1).build();
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, pixel_format);
}

bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const fuchsia_sysmem_ColorSpace& color_space_v1,
    const fuchsia_sysmem_PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  ColorSpace color_space = sysmem::V2CopyFromV1ColorSpace(&allocator, color_space_v1).build();
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, pixel_format);
}

bool ImageFormatIsSupported(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  for (auto format_set : kImageFormats) {
    if (format_set->IsSupported(pixel_format)) {
      return true;
    }
  }
  return false;
}

bool ImageFormatIsSupported(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatIsSupported(pixel_format);
}

bool ImageFormatIsSupported(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatIsSupported(pixel_format);
}

uint32_t ImageFormatBitsPerPixel(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::INVALID:
    case PixelFormatType::MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::R8G8B8A8:
      return 4u * 8u;
    case PixelFormatType::BGRA32:
      return 4u * 8u;
    case PixelFormatType::BGR24:
      return 3u * 8u;
    case PixelFormatType::I420:
      return 12u;
    case PixelFormatType::M420:
      return 12u;
    case PixelFormatType::NV12:
      return 12u;
    case PixelFormatType::YUY2:
      return 2u * 8u;
    case PixelFormatType::YV12:
      return 12u;
    case PixelFormatType::RGB565:
      return 16u;
    case PixelFormatType::RGB332:
    case PixelFormatType::RGB2220:
    case PixelFormatType::L8:
      return 8u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format.type()));
  return 0u;
}

uint32_t ImageFormatBitsPerPixel(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatBitsPerPixel(pixel_format);
}

// Overall bits per pixel, across all pixel data in the whole image.
uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatBitsPerPixel(pixel_format);
}

uint32_t ImageFormatStrideBytesPerWidthPixel(
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  // This list should match the one in garnet/public/rust/fuchsia-framebuffer/src/sysmem.rs.
  switch (pixel_format.type()) {
    case PixelFormatType::INVALID:
    case PixelFormatType::MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::R8G8B8A8:
      return 4u;
    case PixelFormatType::BGRA32:
      return 4u;
    case PixelFormatType::BGR24:
      return 3u;
    case PixelFormatType::I420:
      return 1u;
    case PixelFormatType::M420:
      return 1u;
    case PixelFormatType::NV12:
      return 1u;
    case PixelFormatType::YUY2:
      return 2u;
    case PixelFormatType::YV12:
      return 1u;
    case PixelFormatType::RGB565:
      return 2u;
    case PixelFormatType::RGB332:
      return 1u;
    case PixelFormatType::RGB2220:
      return 1u;
    case PixelFormatType::L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format.type()));
  return 0u;
}

uint32_t ImageFormatStrideBytesPerWidthPixel(
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatStrideBytesPerWidthPixel(pixel_format);
}

uint32_t ImageFormatStrideBytesPerWidthPixel(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatStrideBytesPerWidthPixel(pixel_format);
}

uint64_t ImageFormatImageSize(const llcpp::fuchsia::sysmem2::ImageFormat& image_format) {
  ZX_DEBUG_ASSERT(image_format.has_pixel_format());
  for (auto format_set : kImageFormats) {
    if (format_set->IsSupported(image_format.pixel_format())) {
      return format_set->ImageFormatImageSize(image_format);
    }
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(image_format.pixel_format().type()));
  return 0;
}

uint64_t ImageFormatImageSize(const llcpp::fuchsia::sysmem::ImageFormat_2& image_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  ImageFormat image_format =
      sysmem::V2CopyFromV1ImageFormat(&allocator, image_format_v1).take_value().build();
  return ImageFormatImageSize(image_format);
}

uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format_v1) {
  ZX_DEBUG_ASSERT(image_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  ImageFormat image_format =
      sysmem::V2CopyFromV1ImageFormat(&allocator, *image_format_v1).take_value().build();
  return ImageFormatImageSize(image_format);
}

uint32_t ImageFormatCodedWidthMinDivisor(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::INVALID:
    case PixelFormatType::MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::R8G8B8A8:
      return 1u;
    case PixelFormatType::BGRA32:
      return 1u;
    case PixelFormatType::BGR24:
      return 1u;
    case PixelFormatType::I420:
      return 2u;
    case PixelFormatType::M420:
      return 2u;
    case PixelFormatType::NV12:
      return 2u;
    case PixelFormatType::YUY2:
      return 2u;
    case PixelFormatType::YV12:
      return 2u;
    case PixelFormatType::RGB565:
      return 1u;
    case PixelFormatType::RGB332:
      return 1u;
    case PixelFormatType::RGB2220:
      return 1u;
    case PixelFormatType::L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format.type()));
  return 0u;
}

uint32_t ImageFormatCodedWidthMinDivisor(
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatCodedWidthMinDivisor(pixel_format);
}

uint32_t ImageFormatCodedWidthMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatCodedWidthMinDivisor(pixel_format);
}

uint32_t ImageFormatCodedHeightMinDivisor(
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::INVALID:
    case PixelFormatType::MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::R8G8B8A8:
      return 1u;
    case PixelFormatType::BGRA32:
      return 1u;
    case PixelFormatType::BGR24:
      return 1u;
    case PixelFormatType::I420:
      return 2u;
    case PixelFormatType::M420:
      return 2u;
    case PixelFormatType::NV12:
      return 2u;
    case PixelFormatType::YUY2:
      return 2u;
    case PixelFormatType::YV12:
      return 2u;
    case PixelFormatType::RGB565:
      return 1u;
    case PixelFormatType::RGB332:
      return 1u;
    case PixelFormatType::RGB2220:
      return 1u;
    case PixelFormatType::L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format.type()));
  return 0u;
}

uint32_t ImageFormatCodedHeightMinDivisor(
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatCodedHeightMinDivisor(pixel_format);
}

uint32_t ImageFormatCodedHeightMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatCodedHeightMinDivisor(pixel_format);
}

uint32_t ImageFormatSampleAlignment(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::INVALID:
    case PixelFormatType::MJPEG:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::R8G8B8A8:
      return 4u;
    case PixelFormatType::BGRA32:
      return 4u;
    case PixelFormatType::BGR24:
      return 1u;
    case PixelFormatType::I420:
      return 2u;
    case PixelFormatType::M420:
      return 2u;
    case PixelFormatType::NV12:
      return 2u;
    case PixelFormatType::YUY2:
      return 2u;
    case PixelFormatType::YV12:
      return 2u;
    case PixelFormatType::RGB565:
      return 2u;
    case PixelFormatType::RGB332:
      return 1u;
    case PixelFormatType::RGB2220:
      return 1u;
    case PixelFormatType::L8:
      return 1u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format.type()));
  return 0u;
}

uint32_t ImageFormatSampleAlignment(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatSampleAlignment(pixel_format);
}

uint32_t ImageFormatSampleAlignment(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatSampleAlignment(pixel_format);
}

bool ImageFormatMinimumRowBytes(const llcpp::fuchsia::sysmem2::ImageFormatConstraints& constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out) {
  ZX_DEBUG_ASSERT(minimum_row_bytes_out);
  // Caller must set pixel_format.
  ZX_DEBUG_ASSERT(constraints.has_pixel_format());
  // Bytes per row is not well-defined for tiled types.
  if (constraints.pixel_format().has_format_modifier_value() &&
      constraints.pixel_format().format_modifier_value() !=
          llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_LINEAR &&
      constraints.pixel_format().format_modifier_value() !=
          llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_LINEAR_TE) {
    return false;
  }
  if ((constraints.has_min_coded_width() && width < constraints.min_coded_width()) ||
      (constraints.has_max_coded_width() && width > constraints.max_coded_width())) {
    return false;
  }
  uint32_t constraints_min_bytes_per_row =
      constraints.has_min_bytes_per_row() ? constraints.min_bytes_per_row() : 0;
  uint32_t constraints_bytes_per_row_divisor =
      constraints.has_bytes_per_row_divisor() ? constraints.bytes_per_row_divisor() : 1;
  // This code should match the code in garnet/public/rust/fuchsia-framebuffer/src/sysmem.rs.
  *minimum_row_bytes_out = fbl::round_up(
      std::max(ImageFormatStrideBytesPerWidthPixel(constraints.pixel_format()) * width,
               constraints_min_bytes_per_row),
      constraints_bytes_per_row_divisor);
  if (constraints.has_max_bytes_per_row() &&
      *minimum_row_bytes_out > constraints.max_bytes_per_row()) {
    return false;
  }
  return true;
}

bool ImageFormatMinimumRowBytes(
    const llcpp::fuchsia::sysmem::ImageFormatConstraints& image_format_constraints_v1,
    uint32_t width, uint32_t* minimum_row_bytes_out) {
  ZX_DEBUG_ASSERT(minimum_row_bytes_out);
  fidl::BufferThenHeapAllocator<680> allocator;
  ImageFormatConstraints image_format_constraints =
      sysmem::V2CopyFromV1ImageFormatConstraints(&allocator, image_format_constraints_v1)
          .take_value()
          .build();
  return ImageFormatMinimumRowBytes(image_format_constraints, width, minimum_row_bytes_out);
}

bool ImageFormatMinimumRowBytes(
    const fuchsia_sysmem_ImageFormatConstraints* image_format_constraints_v1, uint32_t width,
    uint32_t* minimum_row_bytes_out) {
  ZX_DEBUG_ASSERT(image_format_constraints_v1);
  ZX_DEBUG_ASSERT(minimum_row_bytes_out);
  fidl::BufferThenHeapAllocator<680> allocator;
  ImageFormatConstraints image_format_constraints =
      sysmem::V2CopyFromV1ImageFormatConstraints(&allocator, *image_format_constraints_v1)
          .take_value()
          .build();
  return ImageFormatMinimumRowBytes(image_format_constraints, width, minimum_row_bytes_out);
}

bool ImageFormatConvertSysmemToZx(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  if (pixel_format.has_format_modifier_value() &&
      (pixel_format.format_modifier_value() != llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_LINEAR)) {
    return false;
  }
  switch (pixel_format.type()) {
    case PixelFormatType::BGRA32:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_ARGB_8888;
      return true;

    case PixelFormatType::BGR24:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_888;
      return true;

    case PixelFormatType::RGB565:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_565;
      return true;

    case PixelFormatType::RGB332:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_332;
      return true;

    case PixelFormatType::RGB2220:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_2220;
      return true;

    case PixelFormatType::L8:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_MONO_8;
      return true;

    case PixelFormatType::NV12:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_NV12;
      return true;

    default:
      return false;
  }
}

bool ImageFormatConvertSysmemToZx(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  ZX_DEBUG_ASSERT(zx_pixel_format_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatConvertSysmemToZx(pixel_format, zx_pixel_format_out);
}

bool ImageFormatConvertSysmemToZx(const fuchsia_sysmem_PixelFormat* pixel_format_v1,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  ZX_DEBUG_ASSERT(zx_pixel_format_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format_v1).build();
  return ImageFormatConvertSysmemToZx(pixel_format, zx_pixel_format_out);
}

fit::result<llcpp::fuchsia::sysmem2::PixelFormat::Builder> ImageFormatConvertZxToSysmem_v2(
    fidl::Allocator* allocator, zx_pixel_format_t zx_pixel_format) {
  ZX_DEBUG_ASSERT(allocator);
  PixelFormat::Builder v2b = allocator->make_table_builder<PixelFormat>();
  v2b.set_format_modifier_value(
      sysmem::MakeTracking(allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_LINEAR));
  PixelFormatType out_type;
  switch (zx_pixel_format) {
    case ZX_PIXEL_FORMAT_RGB_565:
      out_type = PixelFormatType::RGB565;
      break;

    case ZX_PIXEL_FORMAT_RGB_332:
      out_type = PixelFormatType::RGB332;
      break;

    case ZX_PIXEL_FORMAT_RGB_2220:
      out_type = PixelFormatType::RGB2220;
      break;

    case ZX_PIXEL_FORMAT_ARGB_8888:
      out_type = PixelFormatType::BGRA32;
      break;

    case ZX_PIXEL_FORMAT_RGB_x888:
      // Switch to using alpha.
      out_type = PixelFormatType::BGRA32;
      break;

    case ZX_PIXEL_FORMAT_MONO_8:
      out_type = PixelFormatType::L8;
      break;

    case ZX_PIXEL_FORMAT_NV12:
      out_type = PixelFormatType::NV12;
      break;

    case ZX_PIXEL_FORMAT_RGB_888:
      out_type = PixelFormatType::BGR24;
      break;

    default:
      return fit::error();
  }
  v2b.set_type(sysmem::MakeTracking(allocator, out_type));
  return fit::ok(std::move(v2b));
}

fit::result<llcpp::fuchsia::sysmem::PixelFormat> ImageFormatConvertZxToSysmem_v1(
    fidl::Allocator* allocator, zx_pixel_format_t zx_pixel_format) {
  ZX_DEBUG_ASSERT(allocator);
  auto pixel_format_v2_result = ImageFormatConvertZxToSysmem_v2(allocator, zx_pixel_format);
  if (!pixel_format_v2_result.is_ok()) {
    return fit::error();
  }
  auto pixel_format_v2 = pixel_format_v2_result.take_value().build();
  auto pixel_format_v1 = sysmem::V1CopyFromV2PixelFormat(pixel_format_v2);
  return fit::ok(std::move(pixel_format_v1));
}

bool ImageFormatConvertZxToSysmem(zx_pixel_format_t zx_pixel_format,
                                  fuchsia_sysmem_PixelFormat* pixel_format_out) {
  ZX_DEBUG_ASSERT(pixel_format_out);
  fidl::BufferThenHeapAllocator<64> allocator;
  auto pixel_format_v2_result = ImageFormatConvertZxToSysmem_v2(&allocator, zx_pixel_format);
  if (!pixel_format_v2_result.is_ok()) {
    return false;
  }
  auto pixel_format_v2 = pixel_format_v2_result.take_value();
  pixel_format_out->type = static_cast<fuchsia_sysmem_PixelFormatType>(pixel_format_v2.type());
  pixel_format_out->has_format_modifier = pixel_format_v2.has_format_modifier_value();
  pixel_format_out->format_modifier.value = pixel_format_v2.format_modifier_value();
  return true;
}

// TODO(dustingreen): From here down need to be converted to operate on v2 natively similar to
// above (merged while 1st sysmem v2 CL was in flight):

fit::result<ImageFormat::Builder> ImageConstraintsToFormat(
    fidl::Allocator* allocator, const ImageFormatConstraints& constraints, uint32_t width,
    uint32_t height) {
  if ((constraints.has_min_coded_height() && height < constraints.min_coded_height()) ||
      (constraints.has_max_coded_height() && height > constraints.max_coded_height())) {
    return fit::error();
  }
  if ((constraints.has_min_coded_width() && width < constraints.min_coded_width()) ||
      (constraints.has_max_coded_width() && width > constraints.max_coded_width())) {
    return fit::error();
  }
  ImageFormat::Builder result(allocator->make<ImageFormat::Frame>());
  uint32_t minimum_row_bytes;
  if (ImageFormatMinimumRowBytes(constraints, width, &minimum_row_bytes)) {
    result.set_bytes_per_row(allocator->make<uint32_t>(minimum_row_bytes));
  } else {
    result.set_bytes_per_row(allocator->make<uint32_t>(0));
  }
  result.set_pixel_format(allocator->make<PixelFormat>(
      sysmem::V2ClonePixelFormat(allocator, constraints.pixel_format()).build()));
  result.set_coded_width(allocator->make<uint32_t>(width));
  result.set_coded_height(allocator->make<uint32_t>(height));
  result.set_display_width(allocator->make<uint32_t>(width));
  result.set_display_height(allocator->make<uint32_t>(height));
  if (constraints.has_color_spaces() && constraints.color_spaces().count()) {
    result.set_color_space(allocator->make<ColorSpace>(
        sysmem::V2CloneColorSpace(allocator, constraints.color_spaces()[0]).build()));
  }
  // result's has_pixel_aspect_ratio field remains un-set which is equivalent to false
  return fit::ok(std::move(result));
}

fit::result<llcpp::fuchsia::sysmem::ImageFormat_2> ImageConstraintsToFormat(
    const llcpp::fuchsia::sysmem::ImageFormatConstraints& image_format_constraints_v1,
    uint32_t width, uint32_t height) {
  fidl::BufferThenHeapAllocator<384> allocator;
  ImageFormatConstraints image_format_constraints_v2 =
      sysmem::V2CopyFromV1ImageFormatConstraints(&allocator, image_format_constraints_v1)
          .take_value()
          .build();
  auto v2_out_result =
      ImageConstraintsToFormat(&allocator, image_format_constraints_v2, width, height);
  if (!v2_out_result.is_ok()) {
    return fit::error();
  }
  auto v2_out = v2_out_result.take_value().build();
  auto v1_out_result = sysmem::V1CopyFromV2ImageFormat(v2_out);
  if (!v1_out_result.is_ok()) {
    return fit::error();
  }
  return fit::ok(v1_out_result.take_value());
}

bool ImageConstraintsToFormat(
    const fuchsia_sysmem_ImageFormatConstraints* image_format_constraints_v1, uint32_t width,
    uint32_t height, fuchsia_sysmem_ImageFormat_2* image_format_out) {
  ZX_DEBUG_ASSERT(image_format_constraints_v1);
  ZX_DEBUG_ASSERT(image_format_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  ImageFormatConstraints image_format_constraints_v2 =
      sysmem::V2CopyFromV1ImageFormatConstraints(&allocator, *image_format_constraints_v1)
          .take_value()
          .build();
  auto v2_out_result =
      ImageConstraintsToFormat(&allocator, image_format_constraints_v2, width, height);
  if (!v2_out_result.is_ok()) {
    return false;
  }
  auto v2_out = v2_out_result.take_value().build();
  auto v1_out_result = sysmem::V1CopyFromV2ImageFormat(v2_out);
  if (!v1_out_result.is_ok()) {
    return false;
  }
  // ImageFormat doesn't have any hanldes, so this struct copy works to convert from v1 LLCPP to
  // v1 FIDL C.  We can remove this whole function when we're done moving away from FIDL C.
  static_assert(sizeof(*image_format_out) == sizeof(v1_out_result.value()));
  *image_format_out = *reinterpret_cast<fuchsia_sysmem_ImageFormat_2*>(&v1_out_result.value());
  return true;
}

bool ImageFormatPlaneByteOffset(const ImageFormat& image_format, uint32_t plane,
                                uint64_t* offset_out) {
  ZX_DEBUG_ASSERT(offset_out);
  for (auto& format_set : kImageFormats) {
    if (format_set->IsSupported(image_format.pixel_format())) {
      return format_set->ImageFormatPlaneByteOffset(image_format, plane, offset_out);
    }
  }
  return false;
}

bool ImageFormatPlaneByteOffset(const llcpp::fuchsia::sysmem::ImageFormat_2& image_format,
                                uint32_t plane, uint64_t* offset_out) {
  ZX_DEBUG_ASSERT(offset_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(&allocator, image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value().build();
  return ImageFormatPlaneByteOffset(image_format_v2, plane, offset_out);
}

bool ImageFormatPlaneByteOffset(const fuchsia_sysmem_ImageFormat_2* image_format, uint32_t plane,
                                uint64_t* offset_out) {
  ZX_DEBUG_ASSERT(image_format);
  ZX_DEBUG_ASSERT(offset_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(&allocator, *image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value().build();
  return ImageFormatPlaneByteOffset(image_format_v2, plane, offset_out);
}

bool ImageFormatPlaneRowBytes(const ImageFormat& image_format, uint32_t plane,
                              uint32_t* row_bytes_out) {
  ZX_DEBUG_ASSERT(row_bytes_out);
  for (auto& format_set : kImageFormats) {
    if (format_set->IsSupported(image_format.pixel_format())) {
      return format_set->ImageFormatPlaneRowBytes(image_format, plane, row_bytes_out);
    }
  }
  return false;
}

bool ImageFormatPlaneRowBytes(const llcpp::fuchsia::sysmem::ImageFormat_2& image_format,
                              uint32_t plane, uint32_t* row_bytes_out) {
  ZX_DEBUG_ASSERT(row_bytes_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(&allocator, image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value().build();
  return ImageFormatPlaneRowBytes(image_format_v2, plane, row_bytes_out);
}

bool ImageFormatPlaneRowBytes(const fuchsia_sysmem_ImageFormat_2* image_format, uint32_t plane,
                              uint32_t* row_bytes_out) {
  ZX_DEBUG_ASSERT(image_format);
  ZX_DEBUG_ASSERT(row_bytes_out);
  fidl::BufferThenHeapAllocator<384> allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(&allocator, *image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value().build();
  return ImageFormatPlaneRowBytes(image_format_v2, plane, row_bytes_out);
}

bool ImageFormatCompatibleWithProtectedMemory(
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format) {
  if (!pixel_format.has_format_modifier_value())
    return true;
  constexpr uint64_t kArmLinearFormat = 0x0800000000000000ul;
  switch (pixel_format.format_modifier_value() & ~AfbcFormats::kAfbcModifierMask) {
    case kArmLinearFormat:
    case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16:
    case llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8:
      // TE formats occasionally need CPU writes to the TE buffer.
      return !(pixel_format.format_modifier_value() &
               llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_TE_BIT);

    default:
      return true;
  }
}

bool ImageFormatCompatibleWithProtectedMemory(
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format_v1) {
  fidl::BufferThenHeapAllocator<384> allocator;
  auto pixel_format_v2 = sysmem::V2CopyFromV1PixelFormat(&allocator, pixel_format_v1).build();
  return ImageFormatCompatibleWithProtectedMemory(pixel_format_v2);
}

bool ImageFormatCompatibleWithProtectedMemory(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(pixel_format);
  fidl::BufferThenHeapAllocator<384> allocator;
  auto pixel_format_v2 = sysmem::V2CopyFromV1PixelFormat(&allocator, *pixel_format).build();
  return ImageFormatCompatibleWithProtectedMemory(pixel_format_v2);
}
