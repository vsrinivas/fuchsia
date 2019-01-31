// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include "registers-pipe.h"

namespace registers {

static constexpr uint32_t kTransCount = 4;

enum Trans { TRANS_A, TRANS_B, TRANS_C, TRANS_EDP };

static const Trans kTrans[kTransCount] = {
    TRANS_A, TRANS_B, TRANS_C, TRANS_EDP,
};

// TRANS_HTOTAL, TRANS_HBLANK,
// TRANS_VTOTAL, TRANS_VBLANK
class TransHVTotal : public hwreg::RegisterBase<TransHVTotal, uint32_t> {
public:
    DEF_FIELD(28, 16, count_total); // same as blank_start
    DEF_FIELD(12, 0, count_active); // same as blank_end
};

// TRANS_HSYNC, TRANS_VSYNC
class TransHVSync : public hwreg::RegisterBase<TransHVSync, uint32_t> {
public:
    DEF_FIELD(28, 16, sync_end);
    DEF_FIELD(12, 0, sync_start);
};

// TRANS_DDI_FUNC_CTL
class TransDdiFuncControl : public hwreg::RegisterBase<TransDdiFuncControl, uint32_t> {
public:
    DEF_BIT(31, trans_ddi_function_enable);
    DEF_FIELD(30, 28, ddi_select);

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

// TRANS_CLK_SEL
class TransClockSelect : public hwreg::RegisterBase<TransClockSelect, uint32_t> {
public:
    DEF_FIELD(31, 29, trans_clock_select);
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
    hwreg::RegisterAddr<TransDdiFuncControl> DdiFuncControl() {
        return GetReg<TransDdiFuncControl>(0x60400);
    }
    hwreg::RegisterAddr<TransConf> Conf() { return GetReg<TransConf>(0x70008); }

    hwreg::RegisterAddr<TransClockSelect> ClockSelect() {
        ZX_ASSERT(trans_ != TRANS_EDP);
        // This uses a different offset from the other transcoder registers.
        return hwreg::RegisterAddr<TransClockSelect>(0x46140 + trans_ * 4);
    }
    hwreg::RegisterAddr<TransDataM> DataM() { return GetReg<TransDataM>(0x60030); }
    hwreg::RegisterAddr<TransDataN> DataN() { return GetReg<TransDataN>(0x60034); }
    hwreg::RegisterAddr<TransLinkM> LinkM() { return GetReg<TransLinkM>(0x60040); }
    hwreg::RegisterAddr<TransLinkN> LinkN() { return GetReg<TransLinkN>(0x60044); }
    hwreg::RegisterAddr<TransMsaMisc> MsaMisc() { return GetReg<TransMsaMisc>(0x60410); }

private:
    template <class RegType> hwreg::RegisterAddr<RegType> GetReg(uint32_t base_addr) {
        return hwreg::RegisterAddr<RegType>(base_addr + offset_);
    }

    Trans trans_;
    uint32_t offset_;

};
} // namespace registers
