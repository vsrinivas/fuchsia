// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_TRANSCODER_H
#define REGISTERS_TRANSCODER_H

#include "register_bitfields.h"

namespace registers {

// TRANS_HTOTAL, TRANS_HBLANK,
// TRANS_VTOTAL, TRANS_VBLANK
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransHVTotal : public RegisterBase {
public:
    DEF_FIELD(28, 16, count_total); // same as blank_start
    DEF_FIELD(12, 0, count_active); // same as blank_end
};

// TRANS_HSYNC, TRANS_VSYNC
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransHVSync : public RegisterBase {
public:
    DEF_FIELD(28, 16, sync_end);
    DEF_FIELD(12, 0, sync_start);
};

// TRANS_DATAM1
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransDataM : public RegisterBase {
public:
    DEF_FIELD(30, 25, tu_or_vcpayload_size);
    DEF_FIELD(23, 0, data_m_value);
};

// TRANS_DATAN1
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransDataN : public RegisterBase {
public:
    DEF_FIELD(23, 0, data_n_value);
};

// TRANS_LINKM1
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransLinkM : public RegisterBase {
public:
    DEF_FIELD(23, 0, link_m_value);
};

// TRANS_LINKN1
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransLinkN : public RegisterBase {
public:
    DEF_FIELD(23, 0, link_n_value);
};

// TRANS_DDI_FUNC_CTL
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransDdiFuncControl : public RegisterBase {
public:
    DEF_BIT(31, trans_ddi_function_enable);
    DEF_FIELD(30, 28, ddi_select);

    DEF_FIELD(26, 24, trans_ddi_mode_select);
    static constexpr uint32_t kModeHdmi = 0;
    static constexpr uint32_t kModeDvi = 1;
    static constexpr uint32_t kModeDisplayPortSst = 2;
    static constexpr uint32_t kModeDisplayPortMst = 3;

    DEF_FIELD(22, 20, bits_per_color);
    DEF_FIELD(19, 18, port_sync_mode_master_select);
    DEF_FIELD(17, 16, sync_polarity);
    DEF_BIT(15, port_sync_mode_enable);
    DEF_BIT(8, dp_vc_payload_allocate);
    DEF_FIELD(3, 1, dp_port_width_selection);
};

// TRANS_MSA_MISC: This specifies two bytes to send in DisplayPort's Main
// Stream Attribute (MSA) data.  The Intel docs specify two fields in this
// register, MISC0 and MISC1.  The more specific fields below are specified
// by the DisplayPort spec.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransMsaMisc : public RegisterBase {
public:
    // MISC1
    DEF_FIELD(10, 9, stereo_video);
    DEF_BIT(8, interlaced_vertical_total_even);
    // MISC0
    DEF_FIELD(7, 5, bits_per_color);
    DEF_BIT(4, colorimetry);
    DEF_BIT(3, dynamic_range);
    DEF_FIELD(2, 1, color_format);
    DEF_BIT(0, sync_clock);
};

// TRANS_CONF
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransConf : public RegisterBase {
public:
    DEF_BIT(31, transcoder_enable);
    DEF_BIT(30, transcoder_state);
    DEF_FIELD(22, 21, interlaced_mode);
};

// TRANS_CLK_SEL
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class TransClockSelect : public RegisterBase {
public:
    DEF_FIELD(31, 29, trans_clock_select);
};

class TranscoderRegs {
public:
    TranscoderRegs(uint32_t transcoder_num) : transcoder_num_(transcoder_num)
    {
        DASSERT(transcoder_num < 3 ||   // Transcoders A, B and C
                transcoder_num == 0xf); // TranscoderEDP
        offset_ = transcoder_num * 0x1000;
    }

    auto HTotal() { return GetReg<TransHVTotal>(0x60000); }
    auto HBlank() { return GetReg<TransHVTotal>(0x60004); }
    auto HSync() { return GetReg<TransHVSync>(0x60008); }
    auto VTotal() { return GetReg<TransHVTotal>(0x6000c); }
    auto VBlank() { return GetReg<TransHVTotal>(0x60010); }
    auto VSync() { return GetReg<TransHVSync>(0x60014); }
    auto DataM() { return GetReg<TransDataM>(0x60030); }
    auto DataN() { return GetReg<TransDataN>(0x60034); }
    auto LinkM() { return GetReg<TransLinkM>(0x60040); }
    auto LinkN() { return GetReg<TransLinkN>(0x60044); }
    auto DdiFuncControl() { return GetReg<TransDdiFuncControl>(0x60400); }
    auto MsaMisc() { return GetReg<TransMsaMisc>(0x60410); }
    auto Conf() { return GetReg<TransConf>(0x70008); }

    auto ClockSelect()
    {
        // This uses a different offset from the other transcoder registers.
        return RegisterAddr<TransClockSelect>(0x46140 + transcoder_num_ * 4);
    }

private:
    template <class RegType> RegisterAddr<RegType> GetReg(uint32_t base_addr)
    {
        return RegisterAddr<RegType>(base_addr + offset_);
    }

    uint32_t transcoder_num_;
    uint32_t offset_;
};

} // namespace

#endif // REGISTERS_TRANSCODER_H
