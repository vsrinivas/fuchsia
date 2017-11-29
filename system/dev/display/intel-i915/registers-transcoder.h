// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "registers-base.h"
#include "registers-pipe.h"

namespace registers {

// TRANS_HTOTAL, TRANS_HBLANK,
// TRANS_VTOTAL, TRANS_VBLANK
class TransHVTotal : public RegisterBase<TransHVTotal> {
public:
    DEF_FIELD(28, 16, count_total); // same as blank_start
    DEF_FIELD(12, 0, count_active); // same as blank_end
};

// TRANS_HSYNC, TRANS_VSYNC
class TransHVSync : public RegisterBase<TransHVSync> {
public:
    DEF_FIELD(28, 16, sync_end);
    DEF_FIELD(12, 0, sync_start);
};

// TRANS_DDI_FUNC_CTL
class TransDdiFuncControl : public RegisterBase<TransDdiFuncControl> {
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
    DEF_BIT(8, dp_vc_payload_allocate);
    DEF_FIELD(3, 1, dp_port_width_selection);
};

// TRANS_CONF
class TransConf : public RegisterBase<TransConf> {
public:
    DEF_BIT(31, transcoder_enable);
    DEF_BIT(30, transcoder_state);
    DEF_FIELD(22, 21, interlaced_mode);
};

// TRANS_CLK_SEL
class TransClockSelect : public RegisterBase<TransClockSelect> {
public:
    DEF_FIELD(31, 29, trans_clock_select);
};

class TranscoderRegs {
public:
    TranscoderRegs(Pipe pipe) : pipe_(pipe) {
        offset_ = pipe * 0x1000;
    }

    RegisterAddr<TransHVTotal> HTotal() { return GetReg<TransHVTotal>(0x60000); }
    RegisterAddr<TransHVTotal> HBlank() { return GetReg<TransHVTotal>(0x60004); }
    RegisterAddr<TransHVSync> HSync() { return GetReg<TransHVSync>(0x60008); }
    RegisterAddr<TransHVTotal> VTotal() { return GetReg<TransHVTotal>(0x6000c); }
    RegisterAddr<TransHVTotal> VBlank() { return GetReg<TransHVTotal>(0x60010); }
    RegisterAddr<TransHVSync> VSync() { return GetReg<TransHVSync>(0x60014); }
    RegisterAddr<TransDdiFuncControl> DdiFuncControl() {
        return GetReg<TransDdiFuncControl>(0x60400);
    }
    RegisterAddr<TransConf> Conf() { return GetReg<TransConf>(0x70008); }

    RegisterAddr<TransClockSelect> ClockSelect() {
        // This uses a different offset from the other transcoder registers.
        return RegisterAddr<TransClockSelect>(0x46140 + pipe_ * 4);
    }

private:
    template <class RegType> RegisterAddr<RegType> GetReg(uint32_t base_addr) {
        return RegisterAddr<RegType>(base_addr + offset_);
    }

    Pipe pipe_;
    uint32_t offset_;

};
} // namespace registers
