// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_DDI_H
#define REGISTERS_DDI_H

#include "register_bitfields.h"

// Registers for controlling the DDIs (Digital Display Interfaces).

namespace registers {

class Ddi {
public:
    // Number of DDIs that the hardware provides.
    static constexpr uint32_t kDdiCount = 5;
};

// DDI_AUX_CTL: Control register for the DisplayPort Aux channel
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiAuxControl : public RegisterBase {
public:
    DEF_BIT(31, send_busy);
    DEF_BIT(28, timeout);
    DEF_FIELD(24, 20, message_size);
    DEF_FIELD(4, 0, sync_pulse_count);

    static auto Get(uint32_t ddi_number)
    {
        DASSERT(ddi_number < Ddi::kDdiCount);
        return RegisterAddr<DdiAuxControl>(0x64010 + 0x100 * ddi_number);
    }
};

// DDI_AUX_DATA: Message contents for DisplayPort Aux messages
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiAuxData {
public:
    // There are 5 32-bit words at this offset.
    static uint32_t GetOffset(uint32_t ddi_number)
    {
        DASSERT(ddi_number < Ddi::kDdiCount);
        return 0x64014 + 0x100 * ddi_number;
    }
};

// DDI_BUF_CTL: DDI buffer control.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiBufControl : public RegisterBase {
public:
    DEF_BIT(31, ddi_buffer_enable);
    DEF_FIELD(27, 24, dp_vswing_emp_sel);
    DEF_BIT(16, port_reversal);
    DEF_BIT(7, ddi_idle_status);
    DEF_BIT(4, ddi_a_lane_capability_control);
    DEF_FIELD(3, 1, dp_port_width_selection);
    DEF_BIT(0, init_display_detected);

    static auto Get(uint32_t ddi_number)
    {
        DASSERT(ddi_number < Ddi::kDdiCount);
        return RegisterAddr<DdiBufControl>(0x64000 + 0x100 * ddi_number);
    }
};

// DP_TP_CTL: DisplayPort transport control.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DdiDpTransportControl : public RegisterBase {
public:
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

    static auto Get(uint32_t ddi_number)
    {
        DASSERT(ddi_number < Ddi::kDdiCount);
        return RegisterAddr<DdiDpTransportControl>(0x64040 + 0x100 * ddi_number);
    }
};

} // namespace

#endif // REGISTERS_DDI_H
