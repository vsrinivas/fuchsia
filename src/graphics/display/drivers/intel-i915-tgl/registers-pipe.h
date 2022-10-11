// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_PIPE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_PIPE_H_

#include <assert.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <fuchsia/hardware/intelgpucore/c/banjo.h>
#include <zircon/assert.h>
#include <zircon/pixelformat.h>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

static constexpr uint32_t kImagePlaneCount = 3;
static constexpr uint32_t kCursorPlane = 2;

// PIPE_SRCSZ (Pipe Image Source Size)
//
// All reserved bits are MBZ (must be zero), so this register can be written
// safely without reading it first.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 704-705
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 533-534
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 550-551
class PipeSourceSize : public hwreg::RegisterBase<PipeSourceSize, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x6001c;

  DEF_RSVDZ_FIELD(31, 29);

  // The horizontal size of the image created by the display planes.
  //
  // The value stored in this field is the horizontal size in pixels, minus one.
  //
  // On Kaby Lake and Skylake, when Frame Buffer Compression or Panel Fitting
  // are in use, the maximum supported image size is 4096 pixels.
  DEF_FIELD(28, 16, horizontal_source_size_minus_one);

  DEF_RSVDZ_FIELD(15, 13);

  // The vertical size of the image created by the display planes.
  //
  // The value stored in this field is the vertical size in pixels, minus one.
  //
  // On Tiger Lake, the maximum supported image size is 4320 pixels.
  //
  // On Kaby Lake and Skylake, the maximum supported image size is 4096 pixels.
  // The field is documented as taking up bits 11:0, and bit 12 is reserved MBZ
  // (Must Be Zero). Our field declaration will respect the MBZ constraint, as
  // long as we obey the maximum vertical image size.
  DEF_FIELD(12, 0, vertical_source_size_minus_one);
};

// PIPE_BOTTOM_COLOR
class PipeBottomColor : public hwreg::RegisterBase<PipeBottomColor, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70034;

  DEF_BIT(31, gamma_enable);
  DEF_BIT(30, csc_enable);
  DEF_FIELD(29, 20, r);
  DEF_FIELD(19, 10, g);
  DEF_FIELD(9, 0, b);
};

// PLANE_SURF
class PlaneSurface : public hwreg::RegisterBase<PlaneSurface, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x7019c;

  // This field omits the lower 12 bits of the address, so the address
  // must be 4k-aligned.
  static constexpr uint32_t kPageShift = 12;
  DEF_FIELD(31, 12, surface_base_addr);
  static constexpr uint32_t kRShiftCount = 12;
  static constexpr uint32_t kLinearAlignment = 256 * 1024;
  static constexpr uint32_t kXTilingAlignment = 256 * 1024;
  static constexpr uint32_t kYTilingAlignment = 1024 * 1024;

  DEF_BIT(3, ring_flip_source);
};

// PLANE_SURFLIVE
class PlaneSurfaceLive : public hwreg::RegisterBase<PlaneSurfaceLive, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x701ac;

  // This field omits the lower 12 bits of the address, so the address
  // must be 4k-aligned.
  static constexpr uint32_t kPageShift = 12;
  DEF_FIELD(31, 12, surface_base_addr);
};

// PLANE_STRIDE (Plane Stride)
//
// This register is double-buffered. Changes are reflected at the start of the
// next Vblank (vertical blank period) after the PLANE_SURF register is written.
//
// This register can be written safely without reading it first. On Tiger Lake,
// all reserved bits are explicitly documented as MBZ (must be zero). While this
// is not the case for the Kaby Lake and Skylake, experiments and the OpenBSD
// i915 driver suggest that writing zeros to the reserved bits is safe.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 832-836
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 603-606
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 598-600
class PlaneSurfaceStride : public hwreg::RegisterBase<PlaneSurfaceStride, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70188;

  DEF_RSVDZ_FIELD(31, 11);

  // The stride of the plane.
  //
  // Linear memory: the value is a cache line (64 bytes) count.
  // X-Tiled and Y-tiled memory: the value is a number of tiles.
  //
  // The stride must not exceed the size of 8192 pixels.
  //
  // On Kaby Lake and Skylake, the stride size must not exceed 32KB. On Kaby
  // Lake and Skylake, the stride field only takes up bits 9-0.
  DEF_FIELD(10, 0, stride);
};

// PLANE_SIZE
class PlaneSurfaceSize : public hwreg::RegisterBase<PlaneSurfaceSize, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70190;

  DEF_FIELD(28, 16, height_minus_1);
  DEF_FIELD(12, 0, width_minus_1);
};

// Possible values for the `alpha_mode*` fields in plane control registers.
enum class PlaneControlAlphaMode {
  kAlphaIgnored = 0,
  kInvalid = 1,
  kAlphaPreMultiplied = 2,
  kAlphaHardwareMultiply = 3,
};

// PLANE_COLOR_CTL (Plane Color Control)
//
// This register is not documented on Kaby Lake or Skylake. On that hardware,
// some of the fields here are located in the PLANE_CTL register.
//
// All reserved bits are MBZ (must be zero), so this register can be written
// safely without reading it first.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 627-735
class PlaneColorControl : public hwreg::RegisterBase<PlaneColorControl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x701CC;

  DEF_RSVDZ_BIT(31);

  // If true, pipe-level gamma correction is enabled for the plane's pixel data.
  //
  // This field is documented as deprecated in favor of the "Post CSC Gamma
  // Enable" field in the Pipe-specific GAMMA_MODE register.
  DEF_BIT(30, pipe_gamma_enabled_deprecated);

  // If false, the plane removes UV offsets for YUV formats without YUV/RGB CSC.
  //
  // This field is used when the plane's source pixel format is a YUV format,
  // and plane-level YUV to RGB CSC (Color Space Conversion) is disabled. If
  // the field is true, 1/2 offsets on the U and V components are preserved.
  // If the field is false, 1/2 offsets are removed.
  DEF_BIT(29, yuv_offset_preserved);

  // If true, plane-level YUV range correction logic is disabled.
  //
  // Range correction expands YUV components from compressed ranges to the full
  // range of values. The 8-bit compressed ranges are +16 to +235 for the Y
  // component, and -112 to +112 for the U and V components.
  //
  // This field is only effective when the plane has a YUV source pixel format.
  // RGB pixel formats always bypass range correction.
  DEF_BIT(28, yuv_range_correction_disabled);

  DEF_RSVDZ_FIELD(27, 24);

  // If true, pipe-level CSC (Color Space Conversion) and pre-CSC gamma
  // correction are enabled for the plane's pixel data.
  //
  // This field is documented as deprecated in favor of the "Pre CSC Gamma
  // Enable" field in the Pipe-specific GAMMA_MODE register, and the "Pipe CSC
  // Enable" field in the CSC_MODE register.
  DEF_BIT(23, pipe_csc_enabled_deprecated);

  // If true, planel-level CSC (Color Space Conversion) logic is enabled.
  //
  // This field is only effective on planes 1-3.
  DEF_BIT(21, csc_enabled);

  // If true, planel-level input CSC (Color Space Conversion) logic is enabled.
  //
  // This field is only effective on planes 1-3.
  DEF_BIT(20, plane_input_csc_enabled);

  // Documented values for the `csc_mode` field.
  enum class ColorSpaceConversion {
    kBypass = 0,
    kYuvToRgbBt601 = 1,
    kYuvToRgbBt709 = 2,
    kYuvtoRgbBt2020 = 3,
    kRgbBt709toBt2020 = 4,

    // TODO(fxbug.dev/110690): Figure out modeling for invalid values 5-7.
  };

  // Specifies the plane-level CSC (Color Space Conversion) mode.
  //
  // This field is only effective on planes 4-7. The CSC logic in planes 1-3 is
  // configured by PLANE_CSC_* registers.
  DEF_ENUM_FIELD(ColorSpaceConversion, 19, 17, csc_mode);

  DEF_RSVDZ_BIT(16);

  // If true, plane-level post-CSC gamma multi-segment processing is enabled.
  //
  // This logic is intended to support HDR tone mapping.
  DEF_BIT(15, post_csc_gamma_multi_segment_enabled);

  // If true, plane-level pre-CSC gamma correction is enabled.
  DEF_BIT(14, pre_csc_gamma_enabled);

  // If true, plane-level post-CSC gamma correction is disabled.
  DEF_BIT(13, post_csc_gamma_disabled);

  // Possible values for the `gamma_mode` field.
  enum class GammaMode {
    // The table lookup is based on pixel R, G, B component values. The output
    // is an interpolation of the values in the two nearest table entries.
    kDirect = 0,

    // The table lookup is based on a pseudo-luminance (L) for the pixel. An
    // adjustment factor (F) is computed by interpolating the entries in the two
    // nearest table entries. Each output component is the input component
    // multiplied by the adjustment factor F.
    // L = 0.25 * R + 0.625 * G + 0.125 * B.
    //
    // This mode is intended to support HDR tone mapping.
    kMultiply = 1,
  };

  // The mode of operation of the plane's gamma correction logic.
  //
  // This field is ignored if plane-level gamma correction is disabled.
  DEF_ENUM_FIELD(GammaMode, 12, 12, gamma_mode);

  // Possible values for the `gamma_multiplier_format` field.
  enum class GammaMultiplierFormat {
    kU0_24 = 0,
    kU8_16 = 1,
  };

  // Specifies how the gamma table entries are turned into multipliers.
  //
  // This field is ignored if plane-level gamma correction is not operating in
  // multiplication mode.
  DEF_ENUM_FIELD(GammaMultiplierFormat, 11, 11, gamma_multiplier_format);

  DEF_RSVDZ_FIELD(10, 6);

  // Selects the plane's alpha blending mode.
  //
  // The registers PLANE_KEYMSK and PLANE_KEYMAX specify constant plane alpha.
  DEF_ENUM_FIELD(PlaneControlAlphaMode, 5, 4, alpha_mode);

  DEF_RSVDZ_FIELD(3, 0);
};

// PLANE_CTL (Plane Control)
//
// This register is double-buffered. Changes are reflected at the start of the
// next Vblank (vertical blank period) after the PLANE_SURF register is written.
//
// All reserved bits are MBZ (must be zero), so this register can be written
// safely without reading it first.
//
// TODO(fxbug.dev/111517): Split this register definitions into separate
// variants for Tiger Lake vs Kaby Lake / Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 745-753
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 562-569
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 559-566
class PlaneControl : public hwreg::RegisterBase<PlaneControl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70180;

  // If true, the plane generates pixels for display.
  //
  // If false, the plane stops fetching surface memory, and outputs transparent
  // pixels.
  DEF_BIT(31, plane_enabled);

  // If true, pipe-level gamma correction is enabled for the plane's pixel data.
  //
  // Pipe-level gamma correction is separate from plane-level gamma correction.
  //
  // This field only exists on Kaby Lake and Skylake. On Tiger Lake, this field
  // was moved to the PlaneColorControl register, and the underlying bit here
  // is used for another field.
  DEF_BIT(30, pipe_gamma_enabled_kaby_lake);

  // See `yuv_offset_preserved` in PlaneColorControl for details.
  //
  // This field only exists on Kaby Lake and Skylake. On Tiger Lake, this field
  // was moved to the PlaneColorControl register, and the underlying bit here
  // is used for another field.
  DEF_BIT(29, yuv_offset_preserved_kaby_lake);

  // See `yuv_range_correction_disabled` in PlaneColorControl for details.
  //
  // This field only exists on Kaby Lake and Skylake. On Tiger Lake, this field
  // was moved to the PlaneColorControl register, and the underlying bit here
  // is used for another field.
  DEF_BIT(28, yuv_range_correction_disabled_kaby_lake);

  // Number of slots allocated to this plane in pipe slice request arbitration.
  //
  // This field is not documented on Kaby Lake or Skylake. The underlying bits
  // are used by different fields.
  int pipe_slice_request_arbitration_slot_count_tiger_lake() const {
    // The cast is lossless and the addition doesn't overflow (causing UB)
    // because this is a 3-bit field.
    return static_cast<int>(hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), 30, 28).get()) + 1;
  }

  // See `pipe_slice_request_arbitration_slot_count_tiger_lake()` for details.
  PlaneControl& set_pipe_slice_request_arbitration_slot_count_tiger_lake(int slot_count) {
    ZX_DEBUG_ASSERT(slot_count > 0);
    ZX_DEBUG_ASSERT(slot_count <= 8);

    // The cast is lossless and the addition doesn't overflow (causing UB)
    // because this is a 3-bit field.
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), 30, 28).set(slot_count - 1);
    return *this;
  }

  // Documented values for the `source_pixel_format_kaby_lake` field.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 564
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 561
  enum class ColorFormatKabyLake {
    kYuv422Packed = 0b0000,
    kYuv420Planar8bpc = 0b0001,  // NV12, not documented on Skylake
    kRgb2_10_10_10 = 0b0010,
    kRgb8888 = 0b0100,
    kRgb16_16_16_16_float = 0b0110,
    kYuv444Packed8bpc = 0b1000,
    kRgb2_10_10_10XrBias = 0b1010,  // Extended range bias
    kIndexed8bit = 0b1100,
    kRgb565 = 0b1110,

    // TODO(fxbug.dev/110690): Figure out modeling for invalid values, and add
    // a getter for the field.
  };

  // Documented values for the `source_pixel_format_tiger_lake` field.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 749
  enum class ColorFormatTigerLake {
    kYuv422Packed8bpc = 0b00000,
    kYuv422Packed10bpc = 0b00001,  // Y210
    kYuv420Planar8bpc = 0b00010,   // NV12
    kYuv422Packed12bpc = 0b00011,  // Y212
    kRgb2_10_10_10 = 0b00100,
    kYuv422Packed16bpc = 0b00101,  // Y216
    kYuv420Planar10bpc = 0b00110,  // P010. Only supported on HDR planes.
    kYuv444Packed10bpc = 0b00111,  // Y410
    kRgb8888 = 0b01000,
    kYuv444Packed12bpc = 0b01001,     // Y412
    kYuv420Planar12bpc = 0b01010,     // P012. Only supported on HDR planes.
    kYuv444Packed16bpc = 0b01011,     // Y416
    kRgb16_16_16_16_float = 0b01100,  // FP16. Only supported on HDR planes.
    kYuv420Planar16bpc = 0b01110,     // P016. Only supported on HDR planes.
    kYuv444Packed8bpc = 0b10000,
    kRgb2_10_10_10XrBias = 0b10100,  // Extended range bias
    kIndexed8bit = 0b11000,
    kRgb565 = 0b11100,

    // TODO(fxbug.dev/110690): Figure out modeling for invalid values, and add
    // a getter for the field.
  };

  // The source pixel format for the plane.
  //
  // The plane converts the source data to the pipe's pixel format, before the
  // data enters the blending logic. Some formats are only supported by HDR
  // planes.
  //
  // This setter uses the field and color format values documented for Tiger
  // Lake.
  PlaneControl& set_source_pixel_format_tiger_lake(ColorFormatTigerLake format) {
    const uint32_t raw_format = static_cast<uint32_t>(format);
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), 27, 23).set(raw_format);
    return *this;
  }

  // The source pixel format for the plane.
  //
  // The plane converts the source data to the pipe's pixel format, before the
  // data enters the blending logic.
  //
  // This setter uses the field and color format values documented for Kaby Lake
  // and Skylake.
  PlaneControl& set_source_pixel_format_kaby_lake(ColorFormatKabyLake format) {
    const uint32_t raw_format = static_cast<uint32_t>(format);
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), 27, 24).set(raw_format);
    return *this;
  }

  // If true, pipe-level color space conversion is enabled for this plane.
  //
  // The pipe-level CSC (color space conversion) is separate from the
  // plane-level CSC.
  //
  // This field only exists on Kaby Lake and Skylake. On Tiger Lake, this field
  // was moved to the PlaneColorControl register, and the underlying bit here
  // is used for another field.
  DEF_BIT(23, pipe_csc_enabled_kaby_lake);

  // Possible values for the `color_key` field.
  enum class ColorKey : uint32_t {
    kColorKeyDisabled = 0b00,
    kColorKeySource = 0b01,
    kColorKeyDestination = 0b01,
    kColorKeySourceWindow = 0b01,
  };

  // Selects the plane's color keying functionality.
  //
  // Color keying has the following restrictions:
  // * The pixel format must not be 8-bit indexed
  // * If used, Source Key Window and Destination color keying must be enabled
  //   on a pair of adjacent planes on a pipe
  // * Source and Source Window keying must not be used on the bottom active
  //   plane
  // * Destination keying must not be used on the top active plane
  DEF_ENUM_FIELD(ColorKey, 22, 21, color_key);

  // Possible values for the `rgb_color_order` field.
  enum class RgbColorOrder {
    kBgrx = 0,
    kRgbx = 1,
  };

  // Selects the color ordering for most RGB formats.
  //
  // This field is ignored for the following input formats:
  // * XR_BIAS 10:10:10
  // * BGRX 5:6:5
  // * Non-RGB color formats, such as YUV and indexed
  DEF_ENUM_FIELD(RgbColorOrder, 20, 20, rgb_color_order);

  // If true, the plane performs no YUV-to-RGB color conversion.
  //
  // This field is ignored when the plane's source is an RGB format.
  //
  // This field is not documented on Tiger Lake. The underlying bit is used by a
  // different field.
  DEF_BIT(19, yuv_to_rgb_csc_disabled_kaby_lake);

  // If true, this plane stores the Y component of planar YUV420 data.
  //
  // If false, this plane stores the U and V components of planar YUV420 data.
  //
  // This field is used when the source pixel format is a YUV420 planar format
  // (NV12 or P0xx). This field must be set to false for all other formats.
  //
  // Only planes 1-5 can store the U and V components in planar YUV420 data.
  // Only planes 6-7 can store the Y component in planar YUV420 data.
  //
  // This field is not documented on Kaby Lake or Skylake. The underlying bit is
  // used by a different field.
  bool has_y_component_in_planar_yuv420_tiger_lake() const {
    return static_cast<bool>(hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), 19, 19).get());
  }

  // See `has_y_component_in_planar_yuv420_tiger_lake()` for details.
  PlaneControl& set_has_y_component_in_planar_yuv420_tiger_lake(bool has_y_component) {
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), 19, 19).set(has_y_component ? 1 : 0);
    return *this;
  }

  // Possible values for the `yuv_to_rgb_csc_format_kaby_lake` field.
  enum class YuvToRgbConversionKabyLake {
    kBt601 = 0,
    kBt709 = 1,
  };

  // Specifies the YUV format for the plane's YUV-to-RGB color space conversion.
  //
  // This field is ignored when the plane's source is not a YUV format.
  //
  // This field is not documented on Tiger Lake. The underlying bit is reserved
  // MBZ (must be zero).
  DEF_ENUM_FIELD(YuvToRgbConversionKabyLake, 18, 18, yuv_to_rgb_csc_format_kaby_lake);

  // Possible values for the `yuv_422_byte_order` field.
  enum class Yuv422ByteOrder {
    kOrderYuyv = 0b00,
    kOrderUyvy = 0b01,
    kOrderYvyu = 0b10,
    kOrderVyuy = 0b11,
  };

  // Selects the byte order for YUV 4:2:2 data formats.
  //
  // This field is ignored when the plane's source format is not YUV 4:2:2.
  DEF_ENUM_FIELD(Yuv422ByteOrder, 17, 16, yuv_422_byte_order);

  // If true, the display engine will decompress Render-compressed surfaces.
  //
  // Decompression has the following limitations:
  // * Decompression must be left-right cache-line pair
  // * The compressed surface must use Y (Legacy) or YF tiling
  // * Plane rotation must not be set to 90 or 270 degrees
  // * The surface format must be RGB8888, RGB1010102 (only on Tiger Lake), or
  //   FP16 (only on Tiger Lake)
  // * On Kaby Lake and Skylake, decompression is only supported on planes 1-2
  //   of pipes A and B
  DEF_BIT(15, decompress_render_compressed_surfaces);

  // Bit 14 is documented as reserved MBZ (must be zero) on Tiger Lake.
  //
  // On Kaby Lake and Skylake, the documented semantics of bit 14 would warrant
  // the name `trickle_feed_disabled`. However, the documentation states that
  // this bit must not be programmed to 1, suggesting that the feature was
  // probably backed out. For our purposes, it's simpler to just consider the
  // bit MBZ everywhere.
  DEF_RSVDZ_BIT(14);

  // If true, plane-level gamma correction is disabled.
  //
  // This field is not documented on Tiger Lake. The underlying bit is used by a
  // different field.
  DEF_BIT(13, plane_gamma_disabled_kaby_lake);

  // If true, clear color mode is disabled when display decompresses surfaces.
  //
  // This field is ignored if `decompress_render_compressed_surfaces` is false.
  // If `decompress_render_compressed_surfaces` and this field is false (Color
  // Clear is enabled), the color must be set in the PLANE_CC_VAL register
  // before performing a flip via a PLANE_SURF register write.
  //
  // This field is not documented on Kaby Lake and Skylake. That hardware does
  // not support Color Clear with decompression.  The underlying bit is used by a
  // different field.
  bool render_decompression_clear_color_disabled_tiger_lake() const {
    return static_cast<bool>(hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), 13, 13).get());
  }

  // See `render_decompression_clear_color_disabled_tiger_lake()` for details.
  PlaneControl& set_render_decompression_clear_color_disabled_tiger_lake(bool disabled) {
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), 13, 13).set(disabled ? 1 : 0);
    return *this;
  }

  // Documented values for the `surface_tiling` field.
  enum class SurfaceTiling {
    kLinear = 0b000,
    kTilingX = 0b001,
    kTilingYLegacy = 0b100,

    // YF tiling is not supported on Tiger Lake.
    kTilingYFKabyLake = 0b101,

    // TODO(fxbug.dev/110690): Figure out modeling for invalid values 2-3, 6-7.
  };

  // Indicates the tiling used by the plane's surface data.
  //
  // Y tiling is not compatible with interlaced modes. YS tiling is not
  // supported.
  DEF_ENUM_FIELD(SurfaceTiling, 12, 10, surface_tiling);

  // If true, surface MMIO address writes take effect as soon as possible.
  //
  // If false, MMIO writes that change the plane's surface address will take
  // effect synchronously, during vertical blank start.
  DEF_BIT(9, async_surface_address_update_enabled);

  // If true, the plane performs a horizontal flip before any rotation.
  //
  // This field is not documented on Kaby Lake and Skylake. The underlying bit
  // is reserved MBZ (must be zero), which is semantically equivalent to
  // considering that horizontal flipping is not supported on Kaby Lake /
  // Skylake, and must always be disabled.
  DEF_BIT(8, horizontal_flip_tiger_lake);

  // If true, right eye Vblank does not trigger plane surface double-buffering.
  //
  // This field is ignored outside stereo 3D mode. In stereo 3D mode, at least
  // one eye Vblank must be unmasked.
  DEF_BIT(7, stereo_surface_right_eye_vblank_masked);

  // If true, left eye Vblank does not trigger plane surface double-buffering.
  //
  // This field is ignored outside stereo 3D mode. In stereo 3D mode, at least
  // one eye Vblank must be unmasked.
  DEF_BIT(6, stereo_surface_left_eye_vblank_masked);

  // See `alpha_mode` in PlaneColorControl for details.
  //
  // This field only exists on Kaby Lake and Skylake. On Tiger Lake, this field
  // was moved to the PlaneColorControl register, and the underlying bits here
  // are used for other fields.
  DEF_ENUM_FIELD(PlaneControlAlphaMode, 5, 4, alpha_mode_kaby_lake);

  // If true, the display engine will decompress Media-compressed surfaces.
  //
  // This field must not be set to true for a plane where
  // `decompress_render_compressed_surfaces` is true.
  //
  // Media decompression is supported for the following formats: YUV420 planar
  // (NV12, P0xx), YUV422, YUV4444, RGB8888, RGB1010102 and FP16.
  //
  // This field is not documented on Kaby Lake and Skylake. The underlying bit
  // is used by a different field.
  bool decompress_media_compressed_surfaces_tiger_lake() const {
    return static_cast<bool>(hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), 4, 4).get());
  }

  // See `decompress_media_compressed_surfaces_tiger_lake()` for details.
  PlaneControl& set_decompress_media_compressed_surfaces_tiger_lake(bool decompress_media) {
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), 4, 4).set(decompress_media ? 1 : 0);
    return *this;
  }

  // If true, double-buffer updates can be disabled for this plane.
  //
  // This field applies when the DOUBLE_BUFFER_CTL register is used to disable
  // the double-buffering of for all the resources that allow disabling.
  DEF_BIT(3, double_buffer_update_disabling_allowed);

  DEF_RSVDZ_BIT(2);

  // Possible values for the `rotation` field.
  enum class Rotation : uint32_t {
    kIdentity = 0,
    k90degrees = 1,
    k180degrees = 2,
    k270degrees = 3,
  };

  // Selects the hardware rotation performed by the plane.
  //
  // 90 and 270 degree rotations have the following restrictions:
  // * The surface must be Y-Tiled
  // * Interlacing must be disabled
  // * Render-Display compression must be disabled
  DEF_ENUM_FIELD(Rotation, 1, 0, rotation);
};

// PLANE_BUF_CFG (Plane Buffer Config)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 720-724
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 558-561
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 555-558
class PlaneBufferConfig : public hwreg::RegisterBase<PlaneBufferConfig, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x7017c;

  DEF_RSVDZ_FIELD(31, 27);

  // The buffer end position for this plane.
  //
  // On Kaby Lake and Skylake, bit 26 is reserved.
  DEF_FIELD(26, 16, buffer_end);

  DEF_RSVDZ_FIELD(15, 11);

  // The buffer start position for this plane.
  //
  // On Kaby Lake and Skylake, bit 10 is reserved.
  DEF_FIELD(10, 0, buffer_start);
};

// PLANE_WM
class PlaneWm : public hwreg::RegisterBase<PlaneWm, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70140;

  DEF_BIT(31, enable);
  DEF_FIELD(18, 14, lines);
  DEF_FIELD(10, 0, blocks);
};

// PLANE_KEYMSK
class PlaneKeyMask : public hwreg::RegisterBase<PlaneKeyMask, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70198;

  DEF_BIT(31, plane_alpha_enable);
};

// PLANE_KEYMAX
class PlaneKeyMax : public hwreg::RegisterBase<PlaneKeyMax, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x701a0;

  DEF_FIELD(31, 24, plane_alpha_value);
};

// PLANE_OFFSET
class PlaneOffset : public hwreg::RegisterBase<PlaneOffset, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x701a4;

  DEF_FIELD(28, 16, start_y);
  DEF_FIELD(12, 0, start_x);
};

// PLANE_POS
class PlanePosition : public hwreg::RegisterBase<PlanePosition, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x7018c;

  DEF_FIELD(28, 16, y_pos);
  DEF_FIELD(12, 0, x_pos);
};

// PS_CTRL
class PipeScalerCtrl : public hwreg::RegisterBase<PipeScalerCtrl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x68180;

  DEF_BIT(31, enable);
  DEF_FIELD(29, 28, mode);
  static constexpr uint32_t kDynamic = 0;
  static constexpr uint32_t k7x5 = 1;

  DEF_FIELD(27, 25, binding);
  static constexpr uint32_t kPipeScaler = 0;
  static constexpr uint32_t kPlane1 = 1;
  static constexpr uint32_t kPlane2 = 2;
  static constexpr uint32_t kPlane3 = 3;

  DEF_FIELD(24, 23, filter_select);
  static constexpr uint32_t kMedium = 0;
  static constexpr uint32_t kEdgeEnhance = 2;
  static constexpr uint32_t kBilienar = 3;

  static constexpr uint32_t kMinSrcSizePx = 8;
  static constexpr uint32_t kMaxSrcWidthPx = 4096;
  static constexpr uint32_t kPipeABScalersAvailable = 2;
  static constexpr uint32_t kPipeCScalersAvailable = 1;
  static constexpr float k7x5MaxRatio = 2.99f;
  static constexpr float kDynamicMaxRatio = 2.99f;
  static constexpr float kDynamicMaxVerticalRatio2049 = 1.99f;
};

// PS_WIN_POS
class PipeScalerWinPosition : public hwreg::RegisterBase<PipeScalerWinPosition, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x68170;

  DEF_FIELD(28, 16, x_pos);
  DEF_FIELD(12, 0, y_pos);
};

// PS_WIN_SIZE
class PipeScalerWinSize : public hwreg::RegisterBase<PipeScalerWinSize, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x68174;

  DEF_FIELD(29, 16, x_size);
  DEF_FIELD(12, 0, y_size);
};

// DE_PIPE_INTERRUPT
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 361-364
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 448-450
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 444-446
class PipeDeInterrupt : public hwreg::RegisterBase<PipeDeInterrupt, uint32_t> {
 public:
  DEF_BIT(1, vsync);
  DEF_BIT(0, vblank);
};

// CUR_BASE
class CursorBase : public hwreg::RegisterBase<CursorBase, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70084;

  DEF_FIELD(31, 12, cursor_base);
  // This field omits the lower 12 bits of the address, so the address
  // must be 4k-aligned.
  static constexpr uint32_t kPageShift = 12;
};

// CUR_CTL
class CursorCtrl : public hwreg::RegisterBase<CursorCtrl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70080;

  DEF_BIT(24, pipe_csc_enable);
  DEF_FIELD(5, 0, mode_select);
  static constexpr uint32_t kDisabled = 0;
  static constexpr uint32_t kArgb128x128 = 34;
  static constexpr uint32_t kArgb256x256 = 35;
  static constexpr uint32_t kArgb64x64 = 39;
};

// CUR_POS
class CursorPos : public hwreg::RegisterBase<CursorPos, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x70088;

  DEF_BIT(31, y_sign);
  DEF_FIELD(27, 16, y_pos);
  DEF_BIT(15, x_sign);
  DEF_FIELD(12, 0, x_pos);
};

// CUR_SURFLIVE
class CursorSurfaceLive : public hwreg::RegisterBase<CursorSurfaceLive, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x700ac;

  static constexpr uint32_t kPageShift = 12;
  DEF_FIELD(31, 12, surface_base_addr);
};

// CSC_COEFF
class CscCoeff : public hwreg::RegisterBase<CscCoeff, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x49010;

  hwreg::BitfieldRef<uint32_t> coefficient(uint32_t i, uint32_t j) {
    ZX_DEBUG_ASSERT(i < 3 && j < 3);
    uint32_t bit = 16 - ((j % 2) * 16);
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 15, bit);
  }
};

class CscCoeffFormat : public hwreg::RegisterBase<CscCoeffFormat, uint16_t> {
 public:
  DEF_BIT(15, sign);
  DEF_FIELD(14, 12, exponent);
  static constexpr uint16_t kExponent0125 = 3;
  static constexpr uint16_t kExponent025 = 2;
  static constexpr uint16_t kExponent05 = 1;
  static constexpr uint16_t kExponent1 = 0;
  static constexpr uint16_t kExponent2 = 7;
  static constexpr uint16_t kExponent4 = 6;
  DEF_FIELD(11, 3, mantissa);
};

// CSC_MODE
class CscMode : public hwreg::RegisterBase<CscMode, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x49028;
};

// CSC_POSTOFF / CSC_PREOFF
class CscOffset : public hwreg::RegisterBase<CscOffset, uint32_t> {
 public:
  static constexpr uint32_t kPostOffsetBaseAddr = 0x49040;
  static constexpr uint32_t kPreOffsetBaseAddr = 0x49030;

  DEF_BIT(12, sign);
  DEF_FIELD(11, 0, magnitude);
};

// An instance of PipeRegs represents the registers for a particular pipe.
class PipeRegs {
 public:
  static constexpr uint32_t kStatusReg = 0x44400;
  static constexpr uint32_t kMaskReg = 0x44404;
  static constexpr uint32_t kIdentityReg = 0x44408;
  static constexpr uint32_t kEnableReg = 0x4440c;

  PipeRegs(Pipe pipe) : pipe_(pipe) {}

  hwreg::RegisterAddr<tgl_registers::PipeSourceSize> PipeSourceSize() {
    return GetReg<tgl_registers::PipeSourceSize>();
  }
  hwreg::RegisterAddr<tgl_registers::PipeBottomColor> PipeBottomColor() {
    return GetReg<tgl_registers::PipeBottomColor>();
  }

  hwreg::RegisterAddr<tgl_registers::PlaneSurface> PlaneSurface(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneSurface>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneSurfaceLive> PlaneSurfaceLive(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneSurfaceLive>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneSurfaceStride> PlaneSurfaceStride(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneSurfaceStride>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneSurfaceSize> PlaneSurfaceSize(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneSurfaceSize>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneColorControl> PlaneColorControlTigerLake(
      int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneColorControl>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneControl> PlaneControl(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneControl>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneOffset> PlaneOffset(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneOffset>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlanePosition> PlanePosition(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlanePosition>(plane_num);
  }
  // 0 == cursor, 1-3 are regular planes
  hwreg::RegisterAddr<tgl_registers::PlaneBufferConfig> PlaneBufCfg(int plane) {
    return hwreg::RegisterAddr<tgl_registers::PlaneBufferConfig>(PlaneBufferConfig::kBaseAddr +
                                                                 0x1000 * pipe_ + 0x100 * plane);
  }

  hwreg::RegisterAddr<tgl_registers::PlaneWm> PlaneWatermark(int plane, int wm_num) {
    return hwreg::RegisterAddr<PlaneWm>(PlaneWm::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane +
                                        4 * wm_num);
  }

  hwreg::RegisterAddr<tgl_registers::PlaneKeyMask> PlaneKeyMask(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneKeyMask>(plane_num);
  }
  hwreg::RegisterAddr<tgl_registers::PlaneKeyMax> PlaneKeyMax(int32_t plane_num) {
    return GetPlaneReg<tgl_registers::PlaneKeyMax>(plane_num);
  }

  hwreg::RegisterAddr<tgl_registers::PipeScalerCtrl> PipeScalerCtrl(int num) {
    return hwreg::RegisterAddr<tgl_registers::PipeScalerCtrl>(PipeScalerCtrl::kBaseAddr +
                                                              0x800 * pipe_ + num * 0x100);
  }

  hwreg::RegisterAddr<tgl_registers::PipeScalerWinPosition> PipeScalerWinPosition(int num) {
    return hwreg::RegisterAddr<tgl_registers::PipeScalerWinPosition>(
        PipeScalerWinPosition::kBaseAddr + 0x800 * pipe_ + num * 0x100);
  }

  hwreg::RegisterAddr<tgl_registers::PipeScalerWinSize> PipeScalerWinSize(int num) {
    return hwreg::RegisterAddr<tgl_registers::PipeScalerWinSize>(PipeScalerWinSize::kBaseAddr +
                                                                 0x800 * pipe_ + num * 0x100);
  }

  hwreg::RegisterAddr<tgl_registers::PipeDeInterrupt> PipeDeInterrupt(uint32_t type) {
    return hwreg::RegisterAddr<tgl_registers::PipeDeInterrupt>(type + 0x10 * pipe_);
  }

  hwreg::RegisterAddr<tgl_registers::CursorBase> CursorBase() {
    return GetReg<tgl_registers::CursorBase>();
  }

  hwreg::RegisterAddr<tgl_registers::CursorCtrl> CursorCtrl() {
    return GetReg<tgl_registers::CursorCtrl>();
  }

  hwreg::RegisterAddr<tgl_registers::CursorPos> CursorPos() {
    return GetReg<tgl_registers::CursorPos>();
  }

  hwreg::RegisterAddr<tgl_registers::CursorSurfaceLive> CursorSurfaceLive() {
    return GetReg<tgl_registers::CursorSurfaceLive>();
  }

  hwreg::RegisterAddr<tgl_registers::CscCoeff> CscCoeff(uint32_t i, uint32_t j) {
    ZX_DEBUG_ASSERT(i < 3 && j < 3);
    uint32_t base = tgl_registers::CscCoeff::kBaseAddr + 4 * ((i * 2) + (j == 2 ? 1 : 0));
    return GetCscReg<tgl_registers::CscCoeff>(base);
  }

  hwreg::RegisterAddr<tgl_registers::CscMode> CscMode() {
    return GetCscReg<tgl_registers::CscMode>(tgl_registers::CscMode::kBaseAddr);
  }

  hwreg::RegisterAddr<tgl_registers::CscOffset> CscOffset(bool preoffset, uint32_t component_idx) {
    uint32_t base =
        (4 * component_idx) + (preoffset ? tgl_registers::CscOffset::kPreOffsetBaseAddr
                                         : tgl_registers::CscOffset::kPostOffsetBaseAddr);
    return GetCscReg<tgl_registers::CscOffset>(base);
  }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg() {
    return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_);
  }

  template <class RegType>
  hwreg::RegisterAddr<RegType> GetPlaneReg(int32_t plane) {
    return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
  }

  template <class RegType>
  hwreg::RegisterAddr<RegType> GetCscReg(uint32_t base) {
    return hwreg::RegisterAddr<RegType>(base + 0x100 * pipe_);
  }

  Pipe pipe_;
};

// Struct of registers which arm double buffered registers
typedef struct pipe_arming_regs {
  uint32_t csc_mode;
  uint32_t pipe_bottom_color;
  uint32_t cur_base;
  uint32_t cur_pos;
  uint32_t plane_surf[kImagePlaneCount];
  uint32_t ps_win_sz[2];
} pipe_arming_regs_t;

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_PIPE_H_
