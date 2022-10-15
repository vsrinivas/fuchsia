// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/image-format/image_format.h"

#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <zircon/assert.h>
#include <zircon/pixelformat.h>

#include <algorithm>
#include <map>
#include <set>

#include <fbl/algorithm.h>

namespace {

using ColorSpace = fuchsia_sysmem2::wire::ColorSpace;
using ColorSpaceType = fuchsia_sysmem2::wire::ColorSpaceType;
using ImageFormat = fuchsia_sysmem2::wire::ImageFormat;
using ImageFormatConstraints = fuchsia_sysmem2::wire::ImageFormatConstraints;
using PixelFormat = fuchsia_sysmem2::wire::PixelFormat;
using PixelFormatType = fuchsia_sysmem2::wire::PixelFormatType;

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
    {ColorSpaceType::kSrgb, {{8, 10, 12, 16}, kColorType_RGB}},
    {ColorSpaceType::kRec601Ntsc, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::kRec601NtscFullRange, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::kRec601Pal, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::kRec601PalFullRange, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::kRec709, {{8, 10}, kColorType_YUV}},
    {ColorSpaceType::kRec2020, {{10, 12}, kColorType_YUV}},
    {ColorSpaceType::kRec2100, {{10, 12}, kColorType_YUV}},
};
const std::map<PixelFormatType, SamplingInfo> kPixelFormatSamplingInfo = {
    {PixelFormatType::kR8G8B8A8, {{8}, kColorType_RGB}},
    {PixelFormatType::kBgra32, {{8}, kColorType_RGB}},
    {PixelFormatType::kI420, {{8}, kColorType_YUV}},
    {PixelFormatType::kM420, {{8}, kColorType_YUV}},
    {PixelFormatType::kNv12, {{8}, kColorType_YUV}},
    {PixelFormatType::kYuy2, {{8}, kColorType_YUV}},
    // 8 bits RGB when uncompressed - in this context, MJPEG is essentially
    // pretending to be uncompressed.
    {PixelFormatType::kMjpeg, {{8}, kColorType_RGB}},
    {PixelFormatType::kYv12, {{8}, kColorType_YUV}},
    {PixelFormatType::kBgr24, {{8}, kColorType_RGB}},

    // These use the same colorspaces as regular 8-bit-per-component formats
    {PixelFormatType::kRgb565, {{8}, kColorType_RGB}},
    {PixelFormatType::kRgb332, {{8}, kColorType_RGB}},
    {PixelFormatType::kRgb2220, {{8}, kColorType_RGB}},
    // Expands to RGB
    {PixelFormatType::kL8, {{8}, kColorType_RGB}},
    {PixelFormatType::kR8, {{8}, kColorType_RGB}},
    {PixelFormatType::kR8G8, {{8}, kColorType_RGB}},
    {PixelFormatType::kA2B10G10R10, {{8}, kColorType_RGB}},
    {PixelFormatType::kA2R10G10B10, {{8}, kColorType_RGB}},
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
    if (pixel_format.type() != PixelFormatType::kR8G8B8A8 &&
        pixel_format.type() != PixelFormatType::kBgra32 &&
        pixel_format.type() != PixelFormatType::kNv12) {
      return false;
    }
    switch (pixel_format.format_modifier_value()) {
      case fuchsia_sysmem2::wire::kFormatModifierIntelI915XTiled:
      case fuchsia_sysmem2::wire::kFormatModifierIntelI915YTiled:
      case fuchsia_sysmem2::wire::kFormatModifierIntelI915YfTiled:
      // X-Tiled CCS is not supported.
      case fuchsia_sysmem2::wire::kFormatModifierIntelI915YTiledCcs:
      case fuchsia_sysmem2::wire::kFormatModifierIntelI915YfTiledCcs:
        return true;
      default:
        return false;
    }
  }

  uint64_t ImageFormatImageSize(const ImageFormat& image_format) const override {
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));

    uint32_t width_in_tiles, height_in_tiles;
    uint32_t num_of_planes = FormatNumOfPlanes(image_format.pixel_format());
    uint64_t size = 0u;

    for (uint32_t plane_idx = 0; plane_idx < num_of_planes; plane_idx += 1) {
      GetSizeInTiles(image_format, plane_idx, &width_in_tiles, &height_in_tiles);
      size += (width_in_tiles * height_in_tiles * kIntelTileByteSize);
    }

    if (FormatHasCcs(image_format.pixel_format())) {
      size += CcsSize(width_in_tiles, height_in_tiles);
    }

    return size;
  }

  bool ImageFormatPlaneByteOffset(const ImageFormat& image_format, uint32_t plane,
                                  uint64_t* offset_out) const override {
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));

    uint32_t num_of_planes = FormatNumOfPlanes(image_format.pixel_format());

    uint32_t end_plane;

    // For image data planes, calculate the size of all previous the image data planes
    if (plane < num_of_planes) {
      end_plane = plane;
    } else if (plane == kCcsPlane) {  // If requesting the CCS Aux plane, calculate the size of all
                                      // the image data planes
      end_plane = num_of_planes;
    } else {  // Plane is out of bounds, return false
      return false;
    }

    uint64_t offset = 0u;
    for (uint32_t plane_idx = 0u; plane_idx < end_plane; plane_idx += 1u) {
      uint32_t width_in_tiles, height_in_tiles;
      GetSizeInTiles(image_format, plane_idx, &width_in_tiles, &height_in_tiles);
      offset += (width_in_tiles * height_in_tiles * kIntelTileByteSize);
    }
    ZX_DEBUG_ASSERT(offset % kIntelTileByteSize == 0);
    *offset_out = offset;
    return true;
  }

  bool ImageFormatPlaneRowBytes(const ImageFormat& image_format, uint32_t plane,
                                uint32_t* row_bytes_out) const override {
    ZX_DEBUG_ASSERT(IsSupported(image_format.pixel_format()));

    uint32_t num_of_planes = FormatNumOfPlanes(image_format.pixel_format());

    if (plane < num_of_planes) {
      uint32_t width_in_tiles, height_in_tiles;
      GetSizeInTiles(image_format, plane, &width_in_tiles, &height_in_tiles);
      const auto& tiling_data =
          GetTilingData(GetTilingTypeForPixelFormat(image_format.pixel_format()));
      *row_bytes_out = width_in_tiles * tiling_data.bytes_per_row_per_tile;
      return true;
    }

    if (plane == kCcsPlane && FormatHasCcs(image_format.pixel_format())) {
      uint32_t width_in_tiles, height_in_tiles;
      // Since we only care about the width, just use the first plane
      GetSizeInTiles(image_format, 0, &width_in_tiles, &height_in_tiles);
      *row_bytes_out =
          CcsWidthInTiles(width_in_tiles) * GetTilingData(TilingType::kY).bytes_per_row_per_tile;
      return true;
    }

    return false;
  }

 private:
  struct TilingData {
    uint32_t tile_rows;
    uint32_t bytes_per_row_per_tile;
  };

  // These are base Intel tilings, with no aux buffers.
  enum class TilingType { kX, kY, kYf };

  // See
  // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-skl-vol05-memory_views.pdf
  static constexpr uint32_t kIntelTileByteSize = 4096;
  static constexpr TilingData kTilingData[] = {
      {
          // kX
          .tile_rows = 8,
          .bytes_per_row_per_tile = 512,
      },
      {
          // kY
          .tile_rows = 32,
          .bytes_per_row_per_tile = 128,
      },
      {
          // kYf
          .tile_rows = 32,
          .bytes_per_row_per_tile = 128,
      },
  };

  // For simplicity CCS plane is always 3, leaving room for Y, U, and V planes if the format is I420
  // or similar.
  static constexpr uint32_t kCcsPlane = 3;

  // See https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-kbl-vol12-display.pdf
  // for a description of the color control surface. The CCS is always Y-tiled. A CCS cache-line
  // (64 bytes, so 2 fit horizontally in a tile) represents 16 horizontal cache line pairs (so 16
  // tiles) and 16 pixels tall.
  static constexpr uint32_t kCcsTileWidthRatio = 2 * 16;
  static constexpr uint32_t kCcsTileHeightRatio = 16;

  static TilingType GetTilingTypeForPixelFormat(PixelFormat pixel_format) {
    switch (pixel_format.format_modifier_value() &
            ~fuchsia_sysmem2::wire::kFormatModifierIntelCcsBit) {
      case fuchsia_sysmem2::wire::kFormatModifierIntelI915XTiled:
        return TilingType::kX;

      case fuchsia_sysmem2::wire::kFormatModifierIntelI915YTiled:
        return TilingType::kY;

      case fuchsia_sysmem2::wire::kFormatModifierIntelI915YfTiled:
        return TilingType::kYf;
      default:
        ZX_DEBUG_ASSERT(false);
        return TilingType::kX;
    }
  }

  static const TilingData& GetTilingData(TilingType type) {
    static_assert(static_cast<size_t>(TilingType::kYf) < std::size(kTilingData));
    ZX_DEBUG_ASSERT(static_cast<uint32_t>(type) < std::size(kTilingData));
    return kTilingData[static_cast<uint32_t>(type)];
  }

  // Gets the total size (in tiles) of image data for non-aux planes
  static void GetSizeInTiles(const ImageFormat& image_format, uint32_t plane, uint32_t* width_out,
                             uint32_t* height_out) {
    const auto& tiling_data =
        GetTilingData(GetTilingTypeForPixelFormat(image_format.pixel_format()));

    const auto& bytes_per_row_per_tile = tiling_data.bytes_per_row_per_tile;
    const auto& tile_rows = tiling_data.tile_rows;

    switch (image_format.pixel_format().type()) {
      case PixelFormatType::kR8G8B8A8:
      case PixelFormatType::kBgra32: {
        // Format only has one plane
        ZX_DEBUG_ASSERT(plane == 0);

        // Both are 32bpp formats
        uint32_t tile_pixel_width = (bytes_per_row_per_tile / 4u);

        *width_out = fbl::round_up(image_format.coded_width(), tile_pixel_width) / tile_pixel_width;
        *height_out = fbl::round_up(image_format.coded_height(), tile_rows) / tile_rows;
      } break;
      // Since NV12 is a biplanar format we must handle the size for each plane separately. From
      // https://github.com/intel/gmmlib/blob/e1f634c5d5a41ac48756b25697ea499605711747/Source/GmmLib/Texture/GmmTextureAlloc.cpp#L1192:
      // "For Tiled Planar surfaces, the planes must be tile-boundary aligned." Meaning that each
      // plane must be separately tiled aligned.
      case PixelFormatType::kNv12:
        if (plane == 0) {
          // Calculate the Y plane size (8 bpp)
          uint32_t tile_pixel_width = bytes_per_row_per_tile;

          *width_out =
              fbl::round_up(image_format.coded_width(), tile_pixel_width) / tile_pixel_width;
          *height_out = fbl::round_up(image_format.coded_height(), tile_rows) / tile_rows;
        } else if (plane == 1) {
          // Calculate the UV plane size (4 bpp)
          // We effectively have 1/2 the height of our original image since we are subsampled at
          // 4:2:0. Since width of the Y plane must match the width of the UV plane we divide the
          // height of the Y plane by 2 to calculate the height of the UV plane (aligned on tile
          // height boundaries). Ensure the height is aligned 2 before dividing.
          uint32_t adjusted_height = fbl::round_up(image_format.coded_height(), 2u) / 2u;

          *width_out = fbl::round_up(image_format.coded_width(), bytes_per_row_per_tile) /
                       bytes_per_row_per_tile;
          *height_out = fbl::round_up(adjusted_height, tile_rows) / tile_rows;
        } else {
          ZX_DEBUG_ASSERT(false);
        }
        break;
      default:
        ZX_DEBUG_ASSERT(false);
        return;
    }
  }

  static bool FormatHasCcs(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
    return pixel_format.format_modifier_value() & fuchsia_sysmem2::wire::kFormatModifierIntelCcsBit;
  }

  // Does not include aux planes
  static uint32_t FormatNumOfPlanes(const PixelFormat& pixel_format) {
    ZX_DEBUG_ASSERT(pixel_format.has_type());
    switch (pixel_format.type()) {
      case PixelFormatType::kR8G8B8A8:
      case PixelFormatType::kBgra32:
        return 1u;
      case PixelFormatType::kNv12:
        return 2u;
      default:
        ZX_DEBUG_ASSERT(false);
        return 0u;
    }
  }

  static uint64_t CcsWidthInTiles(uint32_t main_plane_width_in_tiles) {
    return fbl::round_up(main_plane_width_in_tiles, kCcsTileWidthRatio) / kCcsTileWidthRatio;
  }

  static uint64_t CcsSize(uint32_t width_in_tiles, uint32_t height_in_tiles) {
    uint32_t height_in_ccs_tiles =
        fbl::round_up(height_in_tiles, kCcsTileHeightRatio) / kCcsTileHeightRatio;
    return CcsWidthInTiles(width_in_tiles) * height_in_ccs_tiles * kIntelTileByteSize;
  }
};
class AfbcFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "AfbcFormats"; }

  static constexpr uint64_t kAfbcModifierMask =
      fuchsia_sysmem2::wire::kFormatModifierArmTeBit |
      fuchsia_sysmem2::wire::kFormatModifierArmSplitBlockBit |
      fuchsia_sysmem2::wire::kFormatModifierArmSparseBit |
      fuchsia_sysmem2::wire::kFormatModifierArmYuvBit |
      fuchsia_sysmem2::wire::kFormatModifierArmBchBit |
      fuchsia_sysmem2::wire::kFormatModifierArmTiledHeaderBit;
  bool IsSupported(const PixelFormat& pixel_format) const override {
    if (!pixel_format.has_format_modifier_value())
      return false;
    if (!pixel_format.has_type())
      return false;
    if (pixel_format.type() != PixelFormatType::kR8G8B8A8 &&
        pixel_format.type() != PixelFormatType::kBgra32) {
      return false;
    }
    switch (pixel_format.format_modifier_value() & ~kAfbcModifierMask) {
      case fuchsia_sysmem2::wire::kFormatModifierArmAfbc16X16:
      case fuchsia_sysmem2::wire::kFormatModifierArmAfbc32X8:
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
                        fuchsia_sysmem2::wire::kFormatModifierArmTiledHeaderBit;

    switch (image_format.pixel_format().format_modifier_value() & ~kAfbcModifierMask) {
      case fuchsia_sysmem2::wire::kFormatModifierArmAfbc16X16:
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

      case fuchsia_sysmem2::wire::kFormatModifierArmAfbc32X8:
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
    ZX_DEBUG_ASSERT(image_format.pixel_format().type() == PixelFormatType::kR8G8B8A8 ||
                    image_format.pixel_format().type() == PixelFormatType::kBgra32);
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
        fuchsia_sysmem2::wire::kFormatModifierArmTeBit) {
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
    case PixelFormatType::kR8G8B8A8:
    case PixelFormatType::kBgra32:
    case PixelFormatType::kBgr24:
    case PixelFormatType::kRgb565:
    case PixelFormatType::kRgb332:
    case PixelFormatType::kRgb2220:
    case PixelFormatType::kL8:
    case PixelFormatType::kR8:
    case PixelFormatType::kR8G8:
    case PixelFormatType::kA2B10G10R10:
    case PixelFormatType::kA2R10G10B10:
      return coded_height * bytes_per_row;
    case PixelFormatType::kI420:
      return coded_height * bytes_per_row * 3 / 2;
    case PixelFormatType::kM420:
      return coded_height * bytes_per_row * 3 / 2;
    case PixelFormatType::kNv12:
      return coded_height * bytes_per_row * 3 / 2;
    case PixelFormatType::kYuy2:
      return coded_height * bytes_per_row;
    case PixelFormatType::kYv12:
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
        pixel_format.format_modifier_value() != fuchsia_sysmem2::wire::kFormatModifierLinear) {
      return false;
    }
    ZX_DEBUG_ASSERT(pixel_format.has_type());
    switch (pixel_format.type()) {
      case PixelFormatType::kInvalid:
      case PixelFormatType::kMjpeg:
        return false;
      case PixelFormatType::kR8G8B8A8:
      case PixelFormatType::kBgra32:
      case PixelFormatType::kBgr24:
      case PixelFormatType::kI420:
      case PixelFormatType::kM420:
      case PixelFormatType::kNv12:
      case PixelFormatType::kYuy2:
      case PixelFormatType::kYv12:
      case PixelFormatType::kRgb565:
      case PixelFormatType::kRgb332:
      case PixelFormatType::kRgb2220:
      case PixelFormatType::kL8:
      case PixelFormatType::kR8:
      case PixelFormatType::kR8G8:
      case PixelFormatType::kA2B10G10R10:
      case PixelFormatType::kA2R10G10B10:
        return true;
      default:
        return false;
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
        case PixelFormatType::kNv12:
        case PixelFormatType::kI420:
        case PixelFormatType::kYv12:
          *offset_out = image_format.coded_height() * image_format.bytes_per_row();
          return true;
        default:
          return false;
      }
    }
    if (plane == 2) {
      switch (image_format.pixel_format().type()) {
        case PixelFormatType::kI420:
        case PixelFormatType::kYv12:
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
        case PixelFormatType::kNv12:
          *row_bytes_out = image_format.bytes_per_row();
          return true;
        case PixelFormatType::kI420:
        case PixelFormatType::kYv12:
          *row_bytes_out = image_format.bytes_per_row() / 2;
          return true;
        default:
          return false;
      }
    } else if (plane == 2) {
      switch (image_format.pixel_format().type()) {
        case PixelFormatType::kI420:
        case PixelFormatType::kYv12:
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

class GoldfishFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "GoldfishFormats"; }

  bool IsSupported(const PixelFormat& pixel_format) const override {
    if (!pixel_format.has_type()) {
      return false;
    }
    if (!pixel_format.has_format_modifier_value()) {
      return false;
    }
    switch (pixel_format.format_modifier_value()) {
      case fuchsia_sysmem2::wire::kFormatModifierGoogleGoldfishOptimal:
        return true;
      default:
        return false;
    }
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
      *row_bytes_out = image_format.bytes_per_row();
      return true;
    } else {
      return false;
    }
  }
};

class ArmTELinearFormats : public ImageFormatSet {
 public:
  const char* Name() const override { return "ArmTELinearFormats"; }

  bool IsSupported(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) const override {
    if (!pixel_format.has_format_modifier_value()) {
      return false;
    }
    if (pixel_format.format_modifier_value() != fuchsia_sysmem2::wire::kFormatModifierArmLinearTe)
      return false;
    switch (pixel_format.type()) {
      case PixelFormatType::kInvalid:
      case PixelFormatType::kMjpeg:
        return false;
      case PixelFormatType::kR8G8B8A8:
      case PixelFormatType::kBgra32:
      case PixelFormatType::kBgr24:
      case PixelFormatType::kI420:
      case PixelFormatType::kM420:
      case PixelFormatType::kNv12:
      case PixelFormatType::kYuy2:
      case PixelFormatType::kYv12:
      case PixelFormatType::kRgb565:
      case PixelFormatType::kRgb332:
      case PixelFormatType::kRgb2220:
      case PixelFormatType::kL8:
      case PixelFormatType::kR8:
      case PixelFormatType::kR8G8:
      case PixelFormatType::kA2B10G10R10:
      case PixelFormatType::kA2R10G10B10:
        return true;
      default:
        return false;
    }
    return false;
  }

  uint64_t ImageFormatImageSize(
      const fuchsia_sysmem2::wire::ImageFormat& image_format) const override {
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

  bool ImageFormatPlaneByteOffset(const fuchsia_sysmem2::wire::ImageFormat& image_format,

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

  bool ImageFormatPlaneRowBytes(const fuchsia_sysmem2::wire::ImageFormat& image_format,
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
constexpr GoldfishFormats kGoldfishFormats;

constexpr const ImageFormatSet* kImageFormats[] = {
    &kLinearFormats, &kIntelFormats, &kAfbcFormats, &kArmTELinearFormats, &kGoldfishFormats,
};

}  // namespace

bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem2::wire::PixelFormat& a,
                                   const fuchsia_sysmem2::wire::PixelFormat& b) {
  if (a.type() != b.type()) {
    return false;
  }
  uint64_t format_modifier_a = a.has_format_modifier_value()
                                   ? a.format_modifier_value()
                                   : fuchsia_sysmem2::wire::kFormatModifierNone;
  uint64_t format_modifier_b = b.has_format_modifier_value()
                                   ? b.format_modifier_value()
                                   : fuchsia_sysmem2::wire::kFormatModifierNone;
  if (format_modifier_a != format_modifier_b) {
    return false;
  }
  return true;
}

bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem::wire::PixelFormat& a_v1,
                                   const fuchsia_sysmem::wire::PixelFormat& b_v1) {
  fidl::Arena allocator;
  PixelFormat a = sysmem::V2CopyFromV1PixelFormat(allocator, a_v1);
  PixelFormat b = sysmem::V2CopyFromV1PixelFormat(allocator, b_v1);
  return ImageFormatIsPixelFormatEqual(a, b);
}

bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem_PixelFormat& a_v1,
                                   const fuchsia_sysmem_PixelFormat& b_v1) {
  fidl::Arena allocator;
  PixelFormat a = sysmem::V2CopyFromV1PixelFormat(allocator, a_v1);
  PixelFormat b = sysmem::V2CopyFromV1PixelFormat(allocator, b_v1);
  return ImageFormatIsPixelFormatEqual(a, b);
}

bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const fuchsia_sysmem2::wire::ColorSpace& color_space,
    const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  if (!color_space.has_type())
    return false;
  if (color_space.type() == ColorSpaceType::kPassThrough)
    return true;
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
    const fuchsia_sysmem::wire::ColorSpace& color_space_v1,
    const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  ColorSpace color_space = sysmem::V2CopyFromV1ColorSpace(allocator, color_space_v1);
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, pixel_format);
}

bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const fuchsia_sysmem_ColorSpace& color_space_v1,
    const fuchsia_sysmem_PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  ColorSpace color_space = sysmem::V2CopyFromV1ColorSpace(allocator, color_space_v1);
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatIsSupportedColorSpaceForPixelFormat(color_space, pixel_format);
}

bool ImageFormatIsSupported(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  for (auto format_set : kImageFormats) {
    if (format_set->IsSupported(pixel_format)) {
      return true;
    }
  }
  return false;
}

bool ImageFormatIsSupported(const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatIsSupported(pixel_format);
}

bool ImageFormatIsSupported(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatIsSupported(pixel_format);
}

uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::kInvalid:
    case PixelFormatType::kMjpeg:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::kR8G8B8A8:
      return 4u * 8u;
    case PixelFormatType::kBgra32:
      return 4u * 8u;
    case PixelFormatType::kBgr24:
      return 3u * 8u;
    case PixelFormatType::kI420:
      return 12u;
    case PixelFormatType::kM420:
      return 12u;
    case PixelFormatType::kNv12:
      return 12u;
    case PixelFormatType::kYuy2:
      return 2u * 8u;
    case PixelFormatType::kYv12:
      return 12u;
    case PixelFormatType::kRgb565:
      return 16u;
    case PixelFormatType::kRgb332:
    case PixelFormatType::kRgb2220:
    case PixelFormatType::kL8:
    case PixelFormatType::kR8:
      return 8u;
    case PixelFormatType::kR8G8:
      return 16u;
    case PixelFormatType::kA2B10G10R10:
    case PixelFormatType::kA2R10G10B10:
      return 2u + 3 * 10u;
    default:
      ZX_PANIC("Unknown Pixel Format: %u", sysmem::fidl_underlying_cast(pixel_format.type()));
  }
}

uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatBitsPerPixel(pixel_format);
}

// Overall bits per pixel, across all pixel data in the whole image.
uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatBitsPerPixel(pixel_format);
}

uint32_t ImageFormatStrideBytesPerWidthPixel(
    const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  // This list should match the one in garnet/public/rust/fuchsia-framebuffer/src/sysmem.rs.
  switch (pixel_format.type()) {
    case PixelFormatType::kInvalid:
    case PixelFormatType::kMjpeg:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::kR8G8B8A8:
      return 4u;
    case PixelFormatType::kBgra32:
      return 4u;
    case PixelFormatType::kBgr24:
      return 3u;
    case PixelFormatType::kI420:
      return 1u;
    case PixelFormatType::kM420:
      return 1u;
    case PixelFormatType::kNv12:
      return 1u;
    case PixelFormatType::kYuy2:
      return 2u;
    case PixelFormatType::kYv12:
      return 1u;
    case PixelFormatType::kRgb565:
      return 2u;
    case PixelFormatType::kRgb332:
      return 1u;
    case PixelFormatType::kRgb2220:
      return 1u;
    case PixelFormatType::kL8:
      return 1u;
    case PixelFormatType::kR8:
      return 1u;
    case PixelFormatType::kR8G8:
      return 2u;
    case PixelFormatType::kA2B10G10R10:
      return 4u;
    case PixelFormatType::kA2R10G10B10:
      return 4u;
    default:
      ZX_PANIC("Unknown Pixel Format: %u", sysmem::fidl_underlying_cast(pixel_format.type()));
  }
}

uint32_t ImageFormatStrideBytesPerWidthPixel(
    const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatStrideBytesPerWidthPixel(pixel_format);
}

uint32_t ImageFormatStrideBytesPerWidthPixel(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatStrideBytesPerWidthPixel(pixel_format);
}

uint64_t ImageFormatImageSize(const fuchsia_sysmem2::wire::ImageFormat& image_format) {
  ZX_DEBUG_ASSERT(image_format.has_pixel_format());
  for (auto format_set : kImageFormats) {
    if (format_set->IsSupported(image_format.pixel_format())) {
      return format_set->ImageFormatImageSize(image_format);
    }
  }
  ZX_PANIC("Unknown Pixel Format: %u",
           sysmem::fidl_underlying_cast(image_format.pixel_format().type()));
  return 0;
}

uint64_t ImageFormatImageSize(const fuchsia_sysmem::wire::ImageFormat2& image_format_v1) {
  fidl::Arena allocator;
  ImageFormat image_format =
      sysmem::V2CopyFromV1ImageFormat(allocator, image_format_v1).take_value();
  return ImageFormatImageSize(image_format);
}

uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format_v1) {
  ZX_DEBUG_ASSERT(image_format_v1);
  fidl::Arena allocator;
  ImageFormat image_format =
      sysmem::V2CopyFromV1ImageFormat(allocator, *image_format_v1).take_value();
  return ImageFormatImageSize(image_format);
}

uint32_t ImageFormatCodedWidthMinDivisor(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::kInvalid:
    case PixelFormatType::kMjpeg:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::kR8G8B8A8:
      return 1u;
    case PixelFormatType::kBgra32:
      return 1u;
    case PixelFormatType::kBgr24:
      return 1u;
    case PixelFormatType::kI420:
      return 2u;
    case PixelFormatType::kM420:
      return 2u;
    case PixelFormatType::kNv12:
      return 2u;
    case PixelFormatType::kYuy2:
      return 2u;
    case PixelFormatType::kYv12:
      return 2u;
    case PixelFormatType::kRgb565:
      return 1u;
    case PixelFormatType::kRgb332:
      return 1u;
    case PixelFormatType::kRgb2220:
      return 1u;
    case PixelFormatType::kL8:
      return 1u;
    case PixelFormatType::kR8:
      return 1u;
    case PixelFormatType::kR8G8:
      return 1u;
    case PixelFormatType::kA2B10G10R10:
      return 1u;
    case PixelFormatType::kA2R10G10B10:
      return 1u;
    default:
      ZX_PANIC("Unknown Pixel Format: %u", sysmem::fidl_underlying_cast(pixel_format.type()));
  }
}

uint32_t ImageFormatCodedWidthMinDivisor(const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatCodedWidthMinDivisor(pixel_format);
}

uint32_t ImageFormatCodedWidthMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatCodedWidthMinDivisor(pixel_format);
}

uint32_t ImageFormatCodedHeightMinDivisor(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::kInvalid:
    case PixelFormatType::kMjpeg:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::kR8G8B8A8:
      return 1u;
    case PixelFormatType::kBgra32:
      return 1u;
    case PixelFormatType::kBgr24:
      return 1u;
    case PixelFormatType::kI420:
      return 2u;
    case PixelFormatType::kM420:
      return 2u;
    case PixelFormatType::kNv12:
      return 2u;
    case PixelFormatType::kYuy2:
      return 2u;
    case PixelFormatType::kYv12:
      return 2u;
    case PixelFormatType::kRgb565:
      return 1u;
    case PixelFormatType::kRgb332:
      return 1u;
    case PixelFormatType::kRgb2220:
      return 1u;
    case PixelFormatType::kL8:
      return 1u;
    case PixelFormatType::kR8:
      return 1u;
    case PixelFormatType::kR8G8:
      return 1u;
    case PixelFormatType::kA2B10G10R10:
      return 1u;
    case PixelFormatType::kA2R10G10B10:
      return 1u;
    default:
      ZX_PANIC("Unknown Pixel Format: %u", sysmem::fidl_underlying_cast(pixel_format.type()));
  }
  return 0u;
}

uint32_t ImageFormatCodedHeightMinDivisor(
    const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatCodedHeightMinDivisor(pixel_format);
}

uint32_t ImageFormatCodedHeightMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatCodedHeightMinDivisor(pixel_format);
}

uint32_t ImageFormatSampleAlignment(const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
  switch (pixel_format.type()) {
    case PixelFormatType::kInvalid:
    case PixelFormatType::kMjpeg:
      // impossible; checked previously.
      ZX_DEBUG_ASSERT(false);
      return 0u;
    case PixelFormatType::kR8G8B8A8:
      return 4u;
    case PixelFormatType::kBgra32:
      return 4u;
    case PixelFormatType::kBgr24:
      return 1u;
    case PixelFormatType::kI420:
      return 2u;
    case PixelFormatType::kM420:
      return 2u;
    case PixelFormatType::kNv12:
      return 2u;
    case PixelFormatType::kYuy2:
      return 2u;
    case PixelFormatType::kYv12:
      return 2u;
    case PixelFormatType::kRgb565:
      return 2u;
    case PixelFormatType::kRgb332:
      return 1u;
    case PixelFormatType::kRgb2220:
      return 1u;
    case PixelFormatType::kL8:
      return 1u;
    case PixelFormatType::kR8:
      return 1u;
    case PixelFormatType::kR8G8:
      return 2u;
    case PixelFormatType::kA2B10G10R10:
      return 4u;
    case PixelFormatType::kA2R10G10B10:
      return 4u;
    default:
      ZX_PANIC("Unknown Pixel Format: %u", sysmem::fidl_underlying_cast(pixel_format.type()));
  }
  return 0u;
}

uint32_t ImageFormatSampleAlignment(const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatSampleAlignment(pixel_format);
}

uint32_t ImageFormatSampleAlignment(const fuchsia_sysmem_PixelFormat* pixel_format_v1) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatSampleAlignment(pixel_format);
}

bool ImageFormatMinimumRowBytes(const fuchsia_sysmem2::wire::ImageFormatConstraints& constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out) {
  ZX_DEBUG_ASSERT(minimum_row_bytes_out);
  // Caller must set pixel_format.
  ZX_DEBUG_ASSERT(constraints.has_pixel_format());
  // Bytes per row is not well-defined for tiled types.
  if (constraints.pixel_format().has_format_modifier_value() &&
      constraints.pixel_format().format_modifier_value() !=
          fuchsia_sysmem2::wire::kFormatModifierLinear &&
      constraints.pixel_format().format_modifier_value() !=
          fuchsia_sysmem2::wire::kFormatModifierArmLinearTe) {
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
    const fuchsia_sysmem::wire::ImageFormatConstraints& image_format_constraints_v1, uint32_t width,
    uint32_t* minimum_row_bytes_out) {
  ZX_DEBUG_ASSERT(minimum_row_bytes_out);
  fidl::Arena allocator;
  ImageFormatConstraints image_format_constraints =
      sysmem::V2CopyFromV1ImageFormatConstraints(allocator, image_format_constraints_v1)
          .take_value();
  return ImageFormatMinimumRowBytes(image_format_constraints, width, minimum_row_bytes_out);
}

bool ImageFormatMinimumRowBytes(
    const fuchsia_sysmem_ImageFormatConstraints* image_format_constraints_v1, uint32_t width,
    uint32_t* minimum_row_bytes_out) {
  ZX_DEBUG_ASSERT(image_format_constraints_v1);
  ZX_DEBUG_ASSERT(minimum_row_bytes_out);
  fidl::Arena allocator;
  ImageFormatConstraints image_format_constraints =
      sysmem::V2CopyFromV1ImageFormatConstraints(allocator, *image_format_constraints_v1)
          .take_value();
  return ImageFormatMinimumRowBytes(image_format_constraints, width, minimum_row_bytes_out);
}

bool ImageFormatConvertSysmemToZx(const fuchsia_sysmem2::wire::PixelFormat& pixel_format,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  if (pixel_format.has_format_modifier_value() &&
      (pixel_format.format_modifier_value() != fuchsia_sysmem2::wire::kFormatModifierLinear)) {
    return false;
  }
  switch (pixel_format.type()) {
    case PixelFormatType::kR8G8B8A8:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_ABGR_8888;
      return true;

    case PixelFormatType::kBgra32:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_ARGB_8888;
      return true;

    case PixelFormatType::kBgr24:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_888;
      return true;

    case PixelFormatType::kRgb565:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_565;
      return true;

    case PixelFormatType::kRgb332:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_332;
      return true;

    case PixelFormatType::kRgb2220:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_RGB_2220;
      return true;

    case PixelFormatType::kL8:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_MONO_8;
      return true;

    case PixelFormatType::kNv12:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_NV12;
      return true;

    case PixelFormatType::kA2B10G10R10:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_ABGR_2_10_10_10;
      return true;

    case PixelFormatType::kA2R10G10B10:
      *zx_pixel_format_out = ZX_PIXEL_FORMAT_ARGB_2_10_10_10;
      return true;

    default:
      return false;
  }
}

bool ImageFormatConvertSysmemToZx(const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  ZX_DEBUG_ASSERT(zx_pixel_format_out);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatConvertSysmemToZx(pixel_format, zx_pixel_format_out);
}

bool ImageFormatConvertSysmemToZx(const fuchsia_sysmem_PixelFormat* pixel_format_v1,
                                  zx_pixel_format_t* zx_pixel_format_out) {
  ZX_DEBUG_ASSERT(pixel_format_v1);
  ZX_DEBUG_ASSERT(zx_pixel_format_out);
  fidl::Arena allocator;
  PixelFormat pixel_format = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format_v1);
  return ImageFormatConvertSysmemToZx(pixel_format, zx_pixel_format_out);
}

fpromise::result<fuchsia_sysmem2::wire::PixelFormat> ImageFormatConvertZxToSysmem_v2(
    fidl::AnyArena& allocator, zx_pixel_format_t zx_pixel_format) {
  PixelFormat v2b = PixelFormat(allocator);
  v2b.set_format_modifier_value(allocator, fuchsia_sysmem2::wire::kFormatModifierLinear);
  PixelFormatType out_type;
  switch (zx_pixel_format) {
    case ZX_PIXEL_FORMAT_RGB_565:
      out_type = PixelFormatType::kRgb565;
      break;

    case ZX_PIXEL_FORMAT_RGB_332:
      out_type = PixelFormatType::kRgb332;
      break;

    case ZX_PIXEL_FORMAT_RGB_2220:
      out_type = PixelFormatType::kRgb2220;
      break;

    case ZX_PIXEL_FORMAT_ARGB_8888:
      out_type = PixelFormatType::kBgra32;
      break;

    case ZX_PIXEL_FORMAT_RGB_x888:
      // Switch to using alpha.
      out_type = PixelFormatType::kBgra32;
      break;

    case ZX_PIXEL_FORMAT_MONO_8:
      out_type = PixelFormatType::kL8;
      break;

    case ZX_PIXEL_FORMAT_NV12:
      out_type = PixelFormatType::kNv12;
      break;

    case ZX_PIXEL_FORMAT_RGB_888:
      out_type = PixelFormatType::kBgr24;
      break;

    case ZX_PIXEL_FORMAT_ABGR_8888:
      out_type = PixelFormatType::kR8G8B8A8;
      break;

    case ZX_PIXEL_FORMAT_BGR_888x:
      // Switch to using alpha.
      out_type = PixelFormatType::kR8G8B8A8;
      break;

    default:
      return fpromise::error();
  }
  v2b.set_type(out_type);
  return fpromise::ok(std::move(v2b));
}

fpromise::result<fuchsia_sysmem::wire::PixelFormat> ImageFormatConvertZxToSysmem_v1(
    fidl::AnyArena& allocator, zx_pixel_format_t zx_pixel_format) {
  auto pixel_format_v2_result = ImageFormatConvertZxToSysmem_v2(allocator, zx_pixel_format);
  if (!pixel_format_v2_result.is_ok()) {
    return fpromise::error();
  }
  auto pixel_format_v2 = pixel_format_v2_result.take_value();
  auto pixel_format_v1 = sysmem::V1CopyFromV2PixelFormat(pixel_format_v2);
  return fpromise::ok(std::move(pixel_format_v1));
}

bool ImageFormatConvertZxToSysmem(zx_pixel_format_t zx_pixel_format,
                                  fuchsia_sysmem_PixelFormat* pixel_format_out) {
  ZX_DEBUG_ASSERT(pixel_format_out);
  fidl::Arena allocator;
  auto pixel_format_v2_result = ImageFormatConvertZxToSysmem_v2(allocator, zx_pixel_format);
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

fpromise::result<ImageFormat> ImageConstraintsToFormat(fidl::AnyArena& allocator,
                                                       const ImageFormatConstraints& constraints,
                                                       uint32_t width, uint32_t height) {
  if ((constraints.has_min_coded_height() && height < constraints.min_coded_height()) ||
      (constraints.has_max_coded_height() && height > constraints.max_coded_height())) {
    return fpromise::error();
  }
  if ((constraints.has_min_coded_width() && width < constraints.min_coded_width()) ||
      (constraints.has_max_coded_width() && width > constraints.max_coded_width())) {
    return fpromise::error();
  }
  ImageFormat result(allocator);
  uint32_t minimum_row_bytes;
  if (ImageFormatMinimumRowBytes(constraints, width, &minimum_row_bytes)) {
    result.set_bytes_per_row(minimum_row_bytes);
  } else {
    result.set_bytes_per_row(0);
  }
  result.set_pixel_format(allocator,
                          sysmem::V2ClonePixelFormat(allocator, constraints.pixel_format()));
  result.set_coded_width(width);
  result.set_coded_height(height);
  result.set_display_width(width);
  result.set_display_height(height);
  if (constraints.has_color_spaces() && constraints.color_spaces().count()) {
    result.set_color_space(allocator,
                           sysmem::V2CloneColorSpace(allocator, constraints.color_spaces()[0]));
  }
  // result's has_pixel_aspect_ratio field remains un-set which is equivalent to false
  return fpromise::ok(std::move(result));
}

fpromise::result<fuchsia_sysmem::wire::ImageFormat2> ImageConstraintsToFormat(
    const fuchsia_sysmem::wire::ImageFormatConstraints& image_format_constraints_v1, uint32_t width,
    uint32_t height) {
  fidl::Arena allocator;
  ImageFormatConstraints image_format_constraints_v2 =
      sysmem::V2CopyFromV1ImageFormatConstraints(allocator, image_format_constraints_v1)
          .take_value();
  auto v2_out_result =
      ImageConstraintsToFormat(allocator, image_format_constraints_v2, width, height);
  if (!v2_out_result.is_ok()) {
    return fpromise::error();
  }
  auto v2_out = v2_out_result.take_value();
  auto v1_out_result = sysmem::V1CopyFromV2ImageFormat(v2_out);
  if (!v1_out_result.is_ok()) {
    return fpromise::error();
  }
  return fpromise::ok(v1_out_result.take_value());
}

bool ImageConstraintsToFormat(
    const fuchsia_sysmem_ImageFormatConstraints* image_format_constraints_v1, uint32_t width,
    uint32_t height, fuchsia_sysmem_ImageFormat_2* image_format_out) {
  ZX_DEBUG_ASSERT(image_format_constraints_v1);
  ZX_DEBUG_ASSERT(image_format_out);
  fidl::Arena allocator;
  ImageFormatConstraints image_format_constraints_v2 =
      sysmem::V2CopyFromV1ImageFormatConstraints(allocator, *image_format_constraints_v1)
          .take_value();
  auto v2_out_result =
      ImageConstraintsToFormat(allocator, image_format_constraints_v2, width, height);
  if (!v2_out_result.is_ok()) {
    return false;
  }
  auto v2_out = v2_out_result.take_value();
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

bool ImageFormatPlaneByteOffset(const fuchsia_sysmem::wire::ImageFormat2& image_format,
                                uint32_t plane, uint64_t* offset_out) {
  ZX_DEBUG_ASSERT(offset_out);
  fidl::Arena allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(allocator, image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value();
  return ImageFormatPlaneByteOffset(image_format_v2, plane, offset_out);
}

bool ImageFormatPlaneByteOffset(const fuchsia_sysmem_ImageFormat_2* image_format, uint32_t plane,
                                uint64_t* offset_out) {
  ZX_DEBUG_ASSERT(image_format);
  ZX_DEBUG_ASSERT(offset_out);
  fidl::Arena allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(allocator, *image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value();
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

bool ImageFormatPlaneRowBytes(const fuchsia_sysmem::wire::ImageFormat2& image_format,
                              uint32_t plane, uint32_t* row_bytes_out) {
  ZX_DEBUG_ASSERT(row_bytes_out);
  fidl::Arena allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(allocator, image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value();
  return ImageFormatPlaneRowBytes(image_format_v2, plane, row_bytes_out);
}

bool ImageFormatPlaneRowBytes(const fuchsia_sysmem_ImageFormat_2* image_format, uint32_t plane,
                              uint32_t* row_bytes_out) {
  ZX_DEBUG_ASSERT(image_format);
  ZX_DEBUG_ASSERT(row_bytes_out);
  fidl::Arena allocator;
  auto image_format_v2_result = sysmem::V2CopyFromV1ImageFormat(allocator, *image_format);
  if (!image_format_v2_result.is_ok()) {
    return false;
  }
  auto image_format_v2 = image_format_v2_result.take_value();
  return ImageFormatPlaneRowBytes(image_format_v2, plane, row_bytes_out);
}

bool ImageFormatCompatibleWithProtectedMemory(
    const fuchsia_sysmem2::wire::PixelFormat& pixel_format) {
  if (!pixel_format.has_format_modifier_value())
    return true;
  constexpr uint64_t kArmLinearFormat = 0x0800000000000000ul;
  switch (pixel_format.format_modifier_value() & ~AfbcFormats::kAfbcModifierMask) {
    case kArmLinearFormat:
    case fuchsia_sysmem2::wire::kFormatModifierArmAfbc16X16:
    case fuchsia_sysmem2::wire::kFormatModifierArmAfbc32X8:
      // TE formats occasionally need CPU writes to the TE buffer.
      return !(pixel_format.format_modifier_value() &
               fuchsia_sysmem2::wire::kFormatModifierArmTeBit);

    default:
      return true;
  }
}

bool ImageFormatCompatibleWithProtectedMemory(
    const fuchsia_sysmem::wire::PixelFormat& pixel_format_v1) {
  fidl::Arena allocator;
  auto pixel_format_v2 = sysmem::V2CopyFromV1PixelFormat(allocator, pixel_format_v1);
  return ImageFormatCompatibleWithProtectedMemory(pixel_format_v2);
}

bool ImageFormatCompatibleWithProtectedMemory(const fuchsia_sysmem_PixelFormat* pixel_format) {
  ZX_DEBUG_ASSERT(pixel_format);
  fidl::Arena allocator;
  auto pixel_format_v2 = sysmem::V2CopyFromV1PixelFormat(allocator, *pixel_format);
  return ImageFormatCompatibleWithProtectedMemory(pixel_format_v2);
}
