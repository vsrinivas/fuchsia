// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
    template <class RegType> hwreg::RegisterAddr<RegType> GetReg() {
        return hwreg::RegisterAddr<RegType>((RegType::kBaseAddr + 0x20 * index_) * 4);
    }
    uint32_t index_;
};
}  // namespace registers
