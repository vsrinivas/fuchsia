// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_DDI_H
#define REGISTERS_DDI_H

#include "register_bitfields.h"

// Registers for controlling the DDIs (Digital Display Interfaces).

namespace registers {

// DDI_AUX_CTL: Control register for the DisplayPort Aux channel
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiAuxControl : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x64010;

    DEF_BIT(31, send_busy);
    DEF_BIT(28, timeout);
    DEF_FIELD(24, 20, message_size);
    DEF_FIELD(4, 0, sync_pulse_count);
};

// DDI_AUX_DATA: Message contents for DisplayPort Aux messages
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiAuxData : public RegisterBase {
public:
    // There are 5 32-bit words at this register's address.
    static constexpr uint32_t kBaseAddr = 0x64014;
};

// DDI_BUF_CTL: DDI buffer control.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiBufControl : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x64000;

    DEF_BIT(31, ddi_buffer_enable);
    DEF_FIELD(27, 24, dp_vswing_emp_sel);
    DEF_BIT(16, port_reversal);
    DEF_BIT(7, ddi_idle_status);
    DEF_BIT(4, ddi_a_lane_capability_control);
    DEF_FIELD(3, 1, dp_port_width_selection);
    DEF_BIT(0, init_display_detected);
};

// DP_TP_CTL: DisplayPort transport control.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiDpTransportControl : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x64040;

    DEF_BIT(31, transport_enable);
    DEF_BIT(27, transport_mode_select);
    DEF_BIT(25, force_act);
    DEF_BIT(18, enhanced_framing_enable);

    DEF_FIELD(10, 8, dp_link_training_pattern);
    static constexpr int kTrainingPattern1 = 0;
    static constexpr int kTrainingPattern2 = 1;
    static constexpr int kIdlePattern = 2;
    static constexpr int kSendPixelData = 3;

    DEF_BIT(6, alternate_sr_enable);
};

// An instance of DdiRegs represents the registers for a particular DDI.
class DdiRegs {
public:
    // Number of DDIs that the hardware provides.
    static constexpr uint32_t kDdiCount = 5;

    DdiRegs(uint32_t ddi_number) : ddi_number_(ddi_number) { DASSERT(ddi_number < kDdiCount); }

    auto DdiAuxControl() { return GetReg<registers::DdiAuxControl>(); }
    auto DdiAuxData() { return GetReg<registers::DdiAuxData>(); }
    auto DdiBufControl() { return GetReg<registers::DdiBufControl>(); }
    auto DdiDpTransportControl() { return GetReg<registers::DdiDpTransportControl>(); }

private:
    template <class RegType> RegisterAddr<RegType> GetReg()
    {
        return RegisterAddr<RegType>(RegType::kBaseAddr + 0x100 * ddi_number_);
    }

    uint32_t ddi_number_;
};

} // namespace

#endif // REGISTERS_DDI_H
