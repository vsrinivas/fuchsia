// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_REGISTERS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_REGISTERS_H_

#include <assert.h>

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

namespace registers {
class VdIfGenReg : public hwreg::RegisterBase<VdIfGenReg, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a50;

  DEF_BIT(31, enable_free_clock);
  DEF_BIT(30, sw_reset);
  DEF_BIT(29, reset_on_go_field);
  DEF_BIT(28, urgent_chroma);
  DEF_BIT(27, urgent_luma);
  DEF_BIT(26, chroma_end_at_last_line);
  DEF_BIT(25, luma_end_at_last_line);
  DEF_FIELD(24, 19, hold_lines);
  DEF_BIT(18, last_line);
  DEF_BIT(17, busy);
  DEF_BIT(16, demux_mode);
  DEF_FIELD(15, 14, bytes_per_pixel);
  DEF_FIELD(13, 12, ddr_burst_size_cr);
  DEF_FIELD(11, 10, ddr_burst_size_cb);
  DEF_FIELD(9, 8, ddr_burst_size_y);
  DEF_BIT(7, manual_start_frame);
  DEF_BIT(6, chro_rpt_lastl_ctrl);
  // This seems to do a 128-bit endianness conversion, which isn't very useful. The canvas should
  // be used to do the conversion instead.
  DEF_BIT(4, little_endian);
  DEF_BIT(3, chroma_hz_avg);
  DEF_BIT(2, luma_hz_avg);
  DEF_BIT(1, separate_en);
  DEF_BIT(0, enable);
};

class VdIfCanvas0 : public hwreg::RegisterBase<VdIfCanvas0, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a51;
};

class VdIfLumaX0 : public hwreg::RegisterBase<VdIfLumaX0, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a53;
  DEF_FIELD(30, 16, end);
  DEF_FIELD(14, 0, start);
};

class VdIfLumaY0 : public hwreg::RegisterBase<VdIfLumaY0, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a54;
  DEF_FIELD(28, 16, end);
  DEF_FIELD(12, 0, start);
};

class VdIfChromaX0 : public hwreg::RegisterBase<VdIfChromaX0, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a55;
  DEF_FIELD(30, 16, end);
  DEF_FIELD(14, 0, start);
};

class VdIfChromaY0 : public hwreg::RegisterBase<VdIfChromaY0, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a56;
  DEF_FIELD(28, 16, end);
  DEF_FIELD(12, 0, start);
};

class VdIfGenReg2 : public hwreg::RegisterBase<VdIfGenReg2, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a6d;

  DEF_FIELD(1, 0, color_map);
};

class VdFmtCtrl : public hwreg::RegisterBase<VdFmtCtrl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a68;

  DEF_BIT(31, gate_clk_en);
  DEF_BIT(30, soft_rst);
  DEF_BIT(28, horizontal_repeat);
  DEF_FIELD(27, 24, horizontal_initial_phase);
  DEF_BIT(23, horizontal_repeat_pixel0);
  DEF_FIELD(22, 21, horizontal_yc_ratio);
  DEF_BIT(20, horizontal_enable);
  DEF_BIT(19, virtual_phase0_only);
  DEF_BIT(18, disable_vertical_chroma_repeat);
  DEF_BIT(17, disable_vertical_repeat_line);
  DEF_BIT(16, vertical_repeat_line0);
  DEF_FIELD(15, 12, vertical_skip_line_num);
  DEF_FIELD(11, 8, vertical_initial_phase);
  DEF_FIELD(7, 1, vertical_phase_step);
  DEF_BIT(0, vertical_enable);
};

class VdFmtW : public hwreg::RegisterBase<VdFmtW, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a69;
  DEF_FIELD(27, 16, horizontal_width);
  DEF_FIELD(11, 0, vertical_width);
};

class VdIfRptLoop : public hwreg::RegisterBase<VdIfRptLoop, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a5b;
};

class VdIfLuma0RptPat : public hwreg::RegisterBase<VdIfLuma0RptPat, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a5c;
};

class VdIfChroma0RptPat : public hwreg::RegisterBase<VdIfChroma0RptPat, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a5d;
};

class VdIfLumaPsel : public hwreg::RegisterBase<VdIfLumaPsel, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a60;
};

class VdIfChromaPsel : public hwreg::RegisterBase<VdIfChromaPsel, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a61;
};

class Vd {
 public:
  // 0-based index
  Vd(uint32_t index) : index_(index) { assert(index < 2); }

  auto IfGenReg() { return GetReg<VdIfGenReg>(); }
  auto IfCanvas0() { return GetReg<VdIfCanvas0>(); }
  auto IfLumaX0() { return GetReg<VdIfLumaX0>(); }
  auto IfLumaY0() { return GetReg<VdIfLumaY0>(); }
  auto IfChromaX0() { return GetReg<VdIfChromaX0>(); }
  auto IfChromaY0() { return GetReg<VdIfChromaY0>(); }
  auto IfGenReg2() { return GetReg<VdIfGenReg2>(); }
  auto FmtCtrl() { return GetReg<VdFmtCtrl>(); }
  auto FmtW() { return GetReg<VdFmtW>(); }
  auto IfRptLoop() { return GetReg<VdIfRptLoop>(); }
  auto IfLuma0RptPat() { return GetReg<VdIfLuma0RptPat>(); }
  auto IfChroma0RptPat() { return GetReg<VdIfChroma0RptPat>(); }
  auto IfLumaPsel() { return GetReg<VdIfLumaPsel>(); }
  auto IfChromaPsel() { return GetReg<VdIfChromaPsel>(); }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg() {
    return hwreg::RegisterAddr<RegType>((RegType::kBaseAddr + 0x20 * index_) * 4);
  }
  uint32_t index_;
};

class VpuVppMisc : public hwreg::RegisterBase<VpuVppMisc, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VpuVppMisc>(0x1d26 * 4); }

  DEF_BIT(13, osd2_enable_postblend);
  DEF_BIT(12, osd1_enable_postblend);
  DEF_BIT(11, vd2_enable_postblend);
  DEF_BIT(10, vd1_enable_postblend);
};

class VpuVppOsdScCtrl0 : public hwreg::RegisterBase<VpuVppOsdScCtrl0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VpuVppOsdScCtrl0>(0x1dc8 * 4); }

  enum {
    kSelectOsd1 = 0,
    kSelectOsd2 = 1,
    kSelectVd1 = 2,
    kSelectVd2 = 3,
  };

  DEF_BIT(3, enable);
  DEF_FIELD(1, 0, osd_sc_sel);
};

class VpuVppOsdScoHStartEnd : public hwreg::RegisterBase<VpuVppOsdScoHStartEnd, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VpuVppOsdScoHStartEnd>(0x1dca * 4); }
  DEF_FIELD(27, 16, output_horizontal_start);
  DEF_FIELD(11, 0, output_horizontal_end);
};

class VpuVppOsdScoVStartEnd : public hwreg::RegisterBase<VpuVppOsdScoVStartEnd, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VpuVppOsdScoVStartEnd>(0x1dcb * 4); }
  DEF_FIELD(27, 16, output_vertial_start);
  DEF_FIELD(11, 0, output_vertical_end);
};

class VpuVppOsdSciWhM1 : public hwreg::RegisterBase<VpuVppOsdSciWhM1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VpuVppOsdSciWhM1>(0x1dc9 * 4); }
  DEF_FIELD(28, 16, input_width_minus1);
  DEF_FIELD(12, 0, input_height_minus1);
};

class VpuVppPostblendHSize : public hwreg::RegisterBase<VpuVppPostblendHSize, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VpuVppPostblendHSize>(0x1d21 * 4); }
  DEF_FIELD(11, 0, horizontal_size);
};

class VpuViuOsdBlk0CfgW0 : public hwreg::RegisterBase<VpuViuOsdBlk0CfgW0, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a1b;

  enum {
    kColorMatrix655 = 0,
    kColorMatrix844 = 1,
    kColorMatrix565 = 2,

    kColorMatrixRGBA8888 = 0,
    kColorMatrixARGB8888 = 1,
    kColorMatrixABGR8888 = 2,
    kColorMatrixBGRA8888 = 3,

    kColorMatrixRGB888 = 0,
    kColorMatrixBGR888 = 5,
  };
  enum {
    kBlockMode422 = 3,
    kBlockMode16Bit = 4,
    kBlockMode32Bit = 5,
    kBlockMode24Bit = 7,
  };

  DEF_BIT(29, y_rev);
  DEF_BIT(28, x_rev);
  DEF_FIELD(23, 16, tbl_addr);
  DEF_BIT(15, little_endian);
  DEF_BIT(14, rpt_y);
  DEF_FIELD(13, 12, interp_ctrl);
  DEF_FIELD(11, 8, block_mode);
  DEF_BIT(7, rgb_en);
  DEF_BIT(6, tx_alpha_en);
  DEF_FIELD(5, 2, color_matrix);
  DEF_BIT(1, interlace_en);
  DEF_BIT(0, interlace_sel_odd);
};

class VpuViuOsdBlk0CfgW1 : public hwreg::RegisterBase<VpuViuOsdBlk0CfgW1, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a1c;

  DEF_FIELD(28, 16, virtual_canvas_x_end);
  DEF_FIELD(12, 0, virtual_canvas_x_start);
};

class VpuViuOsdBlk0CfgW2 : public hwreg::RegisterBase<VpuViuOsdBlk0CfgW2, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a1d;

  DEF_FIELD(28, 16, virtual_canvas_y_end);
  DEF_FIELD(12, 0, virtual_canvas_y_start);
};

class VpuViuOsdBlk0CfgW3 : public hwreg::RegisterBase<VpuViuOsdBlk0CfgW3, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a1e;

  DEF_FIELD(28, 16, display_h_end);
  DEF_FIELD(12, 0, display_h_start);
};

class VpuViuOsdBlk0CfgW4 : public hwreg::RegisterBase<VpuViuOsdBlk0CfgW4, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddresses[2] = {0x1a13, 0x1a64};
  DEF_FIELD(28, 16, display_v_end);
  DEF_FIELD(12, 0, display_v_start);
};

class VpuViuOsdCtrlStat2 : public hwreg::RegisterBase<VpuViuOsdCtrlStat2, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a2d;

  DEF_BIT(15, osd_dpath_sel);
  DEF_BIT(14, replaced_alpha_en);
  DEF_FIELD(13, 6, replaced_alpha);
  DEF_FIELD(5, 4, hold_fifo_lines);
  DEF_BIT(3, rgbyuv_full_range);
  DEF_BIT(2, alpha_9b_mode);
  DEF_BIT(0, color_expand_mode);
};

class VpuViuOsdCtrlStat : public hwreg::RegisterBase<VpuViuOsdCtrlStat, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x1a10;

  DEF_BIT(30, enable_free_clock);
  DEF_BIT(28, osd_done);
  DEF_FIELD(27, 24, osd_blk_mode);
  DEF_FIELD(23, 22, osd_blk_ptr);
  DEF_BIT(21, osd_enable);
  DEF_FIELD(20, 12, global_alpha);
  DEF_FIELD(8, 5, ctrl_mtch_y);
  DEF_BIT(4, ctrl_422to444);
  DEF_BIT(0, osd_blk_enable);
};

class Osd {
 public:
  // 0-based OSD index
  Osd(uint32_t index) : index_(index) { assert(index_ < 2); }

  auto CtrlStat() { return GetReg<registers::VpuViuOsdCtrlStat>(); }
  auto CtrlStat2() { return GetReg<registers::VpuViuOsdCtrlStat2>(); }

  auto Blk0CfgW0() { return GetReg<registers::VpuViuOsdBlk0CfgW0>(); }
  auto Blk0CfgW1() { return GetReg<registers::VpuViuOsdBlk0CfgW1>(); }
  auto Blk0CfgW2() { return GetReg<registers::VpuViuOsdBlk0CfgW2>(); }
  auto Blk0CfgW3() { return GetReg<registers::VpuViuOsdBlk0CfgW3>(); }
  auto Blk0CfgW4() { return GetRegNonstandard<registers::VpuViuOsdBlk0CfgW4>(); }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg() {
    return hwreg::RegisterAddr<RegType>((RegType::kBaseAddr + 0x20 * index_) * 4);
  }

  template <class RegType>
  hwreg::RegisterAddr<RegType> GetRegNonstandard() {
    ZX_DEBUG_ASSERT(index_ < countof(RegType::kBaseAddresses));
    return hwreg::RegisterAddr<RegType>(RegType::kBaseAddresses[index_] * 4);
  }

  const uint32_t index_;
};
}  // namespace registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_REGISTERS_H_
