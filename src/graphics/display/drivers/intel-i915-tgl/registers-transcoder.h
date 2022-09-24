// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TRANSCODER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TRANSCODER_H_

#include <optional>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

// TRANS_HTOTAL, TRANS_HBLANK,
// TRANS_VTOTAL, TRANS_VBLANK
class TransHVTotal : public hwreg::RegisterBase<TransHVTotal, uint32_t> {
 public:
  DEF_FIELD(29, 16, count_total);  // same as blank_end
  DEF_FIELD(13, 0, count_active);  // same as blank_start
};

// TRANS_HSYNC, TRANS_VSYNC
class TransHVSync : public hwreg::RegisterBase<TransHVSync, uint32_t> {
 public:
  DEF_FIELD(29, 16, sync_end);
  DEF_FIELD(13, 0, sync_start);
};

// TRANS_VSYNCSHIFT
class TransVSyncShift : public hwreg::RegisterBase<TransVSyncShift, uint32_t> {
 public:
  DEF_FIELD(12, 0, second_field_vsync_shift);
};

// TRANS_DDI_FUNC_CTL
class TransDdiFuncControl : public hwreg::RegisterBase<TransDdiFuncControl, uint32_t> {
 public:
  DEF_BIT(31, trans_ddi_function_enable);

  // Selects the DDI that the transcoder will connect to.
  //
  // This field has a non-trivial value encoding. The ddi_*() and set_ddi_*()
  // helpers should be preferred to accessing the field directly.
  //
  // This field is tagged `_subtle` because the definition matches the bits used
  // on Tiger Lake, but it's used on all supported models. Kaby Lake and Skylake
  // have a very similar field, which only takes up bits 30-28. Fortunately, bit
  // 27 is reserved MBZ (must be zero). So, there's still a 1:1 mapping between
  // DDI selection and the values of bits 30-27.
  //
  // We take advantage of this to avoid forking the entire (fairly large)
  // register definition by papering over this difference in the helpers
  // `ddi_kaby_lake()` and `set_ddi_kaby_lake()`.
  DEF_FIELD(30, 27, ddi_select_subtle);

  // The DDI that the transcoder will connect to.
  //
  // This helper works for Kaby Lake and Skylake.
  //
  // This field must not be changed while `enabled` is true. Directing multiple
  // transcoders to the same DDI is only valid for DisplayPort Multi-Streaming.
  //
  // The underlying field is ignored by the EDP transcoder, which is attached to
  // DDI A.
  std::optional<Ddi> ddi_kaby_lake() const {
    // The cast is lossless because `ddi_select_subtle()` is a 4-bit field.
    const int8_t ddi_select_raw_value = static_cast<int8_t>(ddi_select_subtle());
    if (ddi_select_raw_value == 0) {
      return std::nullopt;
    }

    // Convert from the Tiger Lake field representation.
    const int ddi_index = (ddi_select_raw_value >> 1);
    return static_cast<Ddi>(ddi_index);
  }

  // The DDI that the transcoder will connect to.
  //
  // This helper works for Tiger Lake.
  //
  // This field must not be changed while `enabled` is true. Directing multiple
  // transcoders to the same DDI is only valid for DisplayPort Multi-Streaming.
  //
  // The underlying field is ignored by the DSI transcoders. Each DSI transcoder
  // is attached to a DDI.
  std::optional<Ddi> ddi_tiger_lake() const {
    // The cast is lossless because `ddi_select_subtle()` is a 4-bit field.
    const int8_t ddi_select_raw_value = static_cast<int8_t>(ddi_select_subtle());
    if (ddi_select_raw_value == 0) {
      return std::nullopt;
    }
    const int ddi_index = ddi_select_raw_value - 1;
    return static_cast<Ddi>(ddi_index);
  }

  // See `ddi_kaby_lake()` for details.
  SelfType& set_ddi_kaby_lake(std::optional<Ddi> ddi) {
    if (!ddi.has_value()) {
      return set_ddi_select_subtle(0);
    }

    ZX_DEBUG_ASSERT_MSG(*ddi != Ddi::DDI_A, "DDI A cannot be explicitly connected to a transcoder");
    const int ddi_index = *ddi - Ddi::DDI_A;

    // Convert to the Tiger Lake field representation.
    return set_ddi_select_subtle(ddi_index << 1);
  }

  // See `ddi_tiger_lake()` for details.
  SelfType& set_ddi_tiger_lake(std::optional<Ddi> ddi) {
    if (!ddi.has_value()) {
      return set_ddi_select_subtle(0);
    }
    const int ddi_index = *ddi - Ddi::DDI_A;
    return set_ddi_select_subtle(ddi_index + 1);
  }

  DEF_FIELD(26, 24, trans_ddi_mode_select);
  static constexpr uint32_t kModeHdmi = 0;
  static constexpr uint32_t kModeDvi = 1;
  static constexpr uint32_t kModeDisplayPortSst = 2;
  static constexpr uint32_t kModeDisplayPortMst = 3;

  DEF_FIELD(22, 20, bits_per_color);
  static constexpr uint32_t k8bbc = 0;
  static constexpr uint32_t k10bbc = 1;
  static constexpr uint32_t k6bbc = 2;
  static constexpr uint32_t k12bbc = 3;
  DEF_FIELD(19, 18, port_sync_mode_master_select);
  DEF_FIELD(17, 16, sync_polarity);
  DEF_BIT(15, port_sync_mode_enable);
  DEF_FIELD(14, 12, edp_input_select);
  static constexpr uint32_t kPipeA = 0;
  static constexpr uint32_t kPipeB = 5;
  static constexpr uint32_t kPipeC = 6;
  DEF_BIT(8, dp_vc_payload_allocate);
  DEF_FIELD(3, 1, dp_port_width_selection);
};

// TRANS_CONF
class TransConf : public hwreg::RegisterBase<TransConf, uint32_t> {
 public:
  DEF_BIT(31, transcoder_enable);
  DEF_BIT(30, transcoder_state);
  DEF_FIELD(22, 21, interlaced_mode);
};

// TRANS_CLK_SEL (Transcoder Clock Select).
//
// On Kaby Lake and Skylake, the EDP transcoder always uses the DDI A clock, so
// it doesn't have a Clock Select register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1365-1366
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 947-948
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 922-923
class TranscoderClockSelect : public hwreg::RegisterBase<TranscoderClockSelect, uint32_t> {
 public:
  // Selects the DDI whose port clock is used by this transcoder.
  //
  // This field has a non-trivial value encoding. The ddi_*() and set_ddi_*()
  // helpers should be preferred to accessing the field directly.
  //
  // This field is tagged `_subtle` because the definition matches the bits used
  // on Tiger Lake, but it's used on all supported models. Kaby Lake and Skylake
  // have a very similar field, which only takes up bits 30-28. Fortunately,
  // bit 27 is reserved, and we can still paper over the field width difference
  // in the helpers `ddi_clock_kaby_lake()` and `set_ddi_clock_kaby_lake()`.
  DEF_FIELD(31, 28, ddi_clock_select_subtle);

  // The DDI whose port clock is used by the transcoder.
  //
  // This helper works for Kaby Lake and Skylake.
  //
  // This field must not be changed while the transcoder is enabled.
  std::optional<Ddi> ddi_clock_kaby_lake() const {
    // Shifting converts from the Tiger Lake field width. The cast is lossless
    // because `ddi_clock_select_subtle()` is a 4-bit field.
    const int8_t ddi_clock_select_raw_value = static_cast<int8_t>(ddi_clock_select_subtle() >> 1);
    if (ddi_clock_select_raw_value == 0) {
      return std::nullopt;
    }
    const int ddi_index = ddi_clock_select_raw_value - 1;
    return static_cast<Ddi>(ddi_index);
  }

  // The DDI whose port clock is used by the transcoder.
  //
  // This helper works for Tiger Lake.
  //
  // This field must not be changed while the transcoder is enabled.
  std::optional<Ddi> ddi_clock_tiger_lake() const {
    // The cast is lossless because `ddi_clock_select_subtle()` is a 4-bit field.
    const int8_t ddi_select_raw_value = static_cast<int8_t>(ddi_clock_select_subtle());
    if (ddi_select_raw_value == 0) {
      return std::nullopt;
    }
    const int ddi_index = ddi_select_raw_value - 1;
    return static_cast<Ddi>(ddi_index);
  }

  // See `ddi_clock_kaby_lake()` for details.
  SelfType& set_ddi_clock_kaby_lake(std::optional<Ddi> ddi) {
    ZX_DEBUG_ASSERT_MSG(!ddi.has_value() || ddi != Ddi::DDI_A,
                        "DDI A cannot be explicitly connected to a transcoder");

    const int8_t ddi_select_raw = RawDdiClockSelect(ddi);

    // Convert to the Tiger Lake field representation.
    const uint32_t reserved_bit = (ddi_clock_select_subtle() & 1);
    return set_ddi_clock_select_subtle((ddi_select_raw << 1) | reserved_bit);
  }

  // See `ddi_clock_tiger_lake()` for details.
  SelfType& set_ddi_clock_tiger_lake(std::optional<Ddi> ddi) {
    return set_ddi_clock_select_subtle(RawDdiClockSelect(ddi));
  }

  static auto GetForTranscoder(Trans transcoder) {
    ZX_ASSERT(transcoder >= Trans::TRANS_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder <= Trans::TRANS_C);

    const int transcoder_index = transcoder - Trans::TRANS_A;
    return hwreg::RegisterAddr<TranscoderClockSelect>(0x46140 + 4 * transcoder_index);
  }

 private:
  static int8_t RawDdiClockSelect(std::optional<Ddi> ddi) {
    if (!ddi.has_value()) {
      return 0;
    }
    // The cast is lossless because DDI indices fit in 4 bits.
    const int8_t ddi_index = static_cast<int8_t>(*ddi - Ddi::DDI_A);
    // The addition doesn't overflow and the cast is lossless because DDI
    // indices fit in 4 bits.
    return static_cast<int8_t>(ddi_index + 1);
  }
};

// DATAM
class TransDataM : public hwreg::RegisterBase<TransDataM, uint32_t> {
 public:
  DEF_FIELD(30, 25, tu_or_vcpayload_size);
  DEF_FIELD(23, 0, data_m_value);
};

// DATAN
class TransDataN : public hwreg::RegisterBase<TransDataN, uint32_t> {
 public:
  DEF_FIELD(23, 0, data_n_value);
};

// LINKM1
class TransLinkM : public hwreg::RegisterBase<TransLinkM, uint32_t> {
 public:
  DEF_FIELD(23, 0, link_m_value);
};

// LINKN1
class TransLinkN : public hwreg::RegisterBase<TransLinkN, uint32_t> {
 public:
  DEF_FIELD(23, 0, link_n_value);
};

// TRANS_MSA_MISC
class TransMsaMisc : public hwreg::RegisterBase<TransMsaMisc, uint32_t> {
 public:
  // Byte 1 is MISC1 from DP spec
  DEF_FIELD(10, 9, stereo_video);
  DEF_BIT(8, interlaced_vertical_total_even);
  // Byte 0 is MISC0 from DP spec
  DEF_FIELD(7, 5, bits_per_color);
  static constexpr uint32_t k6Bbc = 0;
  static constexpr uint32_t k8Bbc = 1;
  static constexpr uint32_t k10Bbc = 2;
  static constexpr uint32_t k12Bbc = 3;
  static constexpr uint32_t k16Bbc = 4;
  DEF_BIT(4, colorimetry);
  DEF_BIT(3, dynamic_range);
  DEF_FIELD(2, 1, color_format);
  static constexpr uint32_t kRgb = 0;
  static constexpr uint32_t kYcbCr422 = 1;
  static constexpr uint32_t kYcbCr444 = 2;
  DEF_BIT(0, sync_clock);
};

class TranscoderRegs {
 public:
  TranscoderRegs(Trans trans) : trans_(trans) {
    offset_ = trans == TRANS_EDP ? 0xf000 : (trans * 0x1000);
  }

  hwreg::RegisterAddr<TransHVTotal> HTotal() { return GetReg<TransHVTotal>(0x60000); }
  hwreg::RegisterAddr<TransHVTotal> HBlank() { return GetReg<TransHVTotal>(0x60004); }
  hwreg::RegisterAddr<TransHVSync> HSync() { return GetReg<TransHVSync>(0x60008); }
  hwreg::RegisterAddr<TransHVTotal> VTotal() { return GetReg<TransHVTotal>(0x6000c); }
  hwreg::RegisterAddr<TransHVTotal> VBlank() { return GetReg<TransHVTotal>(0x60010); }
  hwreg::RegisterAddr<TransHVSync> VSync() { return GetReg<TransHVSync>(0x60014); }
  hwreg::RegisterAddr<TransVSyncShift> VSyncShift() { return GetReg<TransVSyncShift>(0x60028); }
  hwreg::RegisterAddr<TransDdiFuncControl> DdiFuncControl() {
    return GetReg<TransDdiFuncControl>(0x60400);
  }
  hwreg::RegisterAddr<TransConf> Conf() { return GetReg<TransConf>(0x70008); }
  hwreg::RegisterAddr<TranscoderClockSelect> ClockSelect() {
    return TranscoderClockSelect::GetForTranscoder(trans_);
  }
  hwreg::RegisterAddr<TransDataM> DataM() { return GetReg<TransDataM>(0x60030); }
  hwreg::RegisterAddr<TransDataN> DataN() { return GetReg<TransDataN>(0x60034); }
  hwreg::RegisterAddr<TransLinkM> LinkM() { return GetReg<TransLinkM>(0x60040); }
  hwreg::RegisterAddr<TransLinkN> LinkN() { return GetReg<TransLinkN>(0x60044); }
  hwreg::RegisterAddr<TransMsaMisc> MsaMisc() { return GetReg<TransMsaMisc>(0x60410); }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg(uint32_t base_addr) {
    return hwreg::RegisterAddr<RegType>(base_addr + offset_);
  }

  Trans trans_;
  uint32_t offset_;
};
}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TRANSCODER_H_
