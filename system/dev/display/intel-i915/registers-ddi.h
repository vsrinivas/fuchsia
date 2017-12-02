// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "registers-base.h"

namespace registers {

// Number of DDIs that the hardware provides.
constexpr uint32_t kDdiCount = 5;

enum Ddi {
    DDI_A, DDI_B, DDI_C, DDI_D, DDI_E
};

static const Ddi kDdis[kDdiCount] = {
    DDI_A, DDI_B, DDI_C, DDI_D, DDI_E,
};

// South Display Engine Interrupt Bit Definition + SINTERRUPT
class SdeInterruptBase : public RegisterBase<SdeInterruptBase> {
public:
    static constexpr uint32_t kSdeIntMask = 0xc4004;
    static constexpr uint32_t kSdeIntIdentity = 0xc4008;
    static constexpr uint32_t kSdeIntEnable = 0xc400c;

    registers::BitfieldRef<uint32_t> ddi_bit(Ddi ddi) {
        uint32_t bit;
        switch (ddi) {
            case DDI_A:
                bit = 24;
                break;
            case DDI_B:
            case DDI_C:
            case DDI_D:
                bit = 20 + ddi;
                break;
            case DDI_E:
                bit = 25;
                break;
            default:
                bit = -1;
        }
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }

    static RegisterAddr<SdeInterruptBase> Get(uint32_t offset) {
        return RegisterAddr<SdeInterruptBase>(offset);
    }
};

// SHOTPLUG_CTL + SHOTPLUG_CTL2
class HotplugCtrl : public RegisterBase<HotplugCtrl> {
public:
    static constexpr uint32_t kOffset = 0xc4030;
    static constexpr uint32_t kOffset2 = 0xc403c;

    static constexpr uint32_t kShortPulseBitSubOffset = 0;
    static constexpr uint32_t kLongPulseBitSubOffset = 1;
    static constexpr uint32_t kHpdEnableBitSubOffset = 4;

    registers::BitfieldRef<uint32_t> hpd_enable(Ddi ddi) {
        uint32_t bit = ddi_to_first_bit(ddi) + kHpdEnableBitSubOffset;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }

    registers::BitfieldRef<uint32_t> long_pulse_detected(Ddi ddi) {
        uint32_t bit = ddi_to_first_bit(ddi) + kLongPulseBitSubOffset;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }

    static RegisterAddr<HotplugCtrl> Get(Ddi ddi) {
        return RegisterAddr<HotplugCtrl>(ddi == DDI_E ? kOffset2 : kOffset);
    }

private:
    static uint32_t ddi_to_first_bit(Ddi ddi) {
        switch (ddi) {
            case DDI_A:
                return 24;
            case DDI_B:
            case DDI_C:
            case DDI_D:
                return 8 * (ddi - 1);
            case DDI_E:
                return 0;
            default:
                return -1;
        }
    }
};

// SFUSE_STRAP
class SouthFuseStrap : public RegisterBase<SouthFuseStrap> {
public:
    DEF_BIT(2, port_b_present);
    DEF_BIT(1, port_c_present);
    DEF_BIT(0, port_d_present);

    static RegisterAddr<SouthFuseStrap> Get() { return RegisterAddr<SouthFuseStrap>(0xc2014); }
};


} // namespace registers
