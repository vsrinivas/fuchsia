// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>

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
class SdeInterruptBase : public hwreg::RegisterBase<SdeInterruptBase, uint32_t> {
public:
    static constexpr uint32_t kSdeIntMask = 0xc4004;
    static constexpr uint32_t kSdeIntIdentity = 0xc4008;
    static constexpr uint32_t kSdeIntEnable = 0xc400c;

    hwreg::BitfieldRef<uint32_t> ddi_bit(Ddi ddi) {
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
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<SdeInterruptBase>(offset); }
};

// SHOTPLUG_CTL + SHOTPLUG_CTL2
class HotplugCtrl : public hwreg::RegisterBase<HotplugCtrl, uint32_t> {
public:
    hwreg::BitfieldRef<uint32_t> hpd_enable(Ddi ddi) {
        uint32_t bit = ddi_to_first_bit(ddi) + kHpdEnableBitSubOffset;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> hpd_long_pulse(Ddi ddi) {
        uint32_t bit = ddi_to_first_bit(ddi) + kHpdLongPulseBitSubOffset;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> hpd_short_pulse(Ddi ddi) {
        uint32_t bit = ddi_to_first_bit(ddi) + kHpdShortPulseBitSubOffset;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static auto Get(Ddi ddi) {
        return hwreg::RegisterAddr<HotplugCtrl>(ddi == DDI_E ? kOffset2 : kOffset);
    }

private:
    static constexpr uint32_t kOffset = 0xc4030;
    static constexpr uint32_t kOffset2 = 0xc403c;

    static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
    static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
    static constexpr uint32_t kHpdEnableBitSubOffset = 4;

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
class SouthFuseStrap : public hwreg::RegisterBase<SouthFuseStrap, uint32_t> {
public:
    DEF_BIT(2, port_b_present);
    DEF_BIT(1, port_c_present);
    DEF_BIT(0, port_d_present);

    static auto Get() { return hwreg::RegisterAddr<SouthFuseStrap>(0xc2014); }
};

// DDI_BUF_CTL
class DdiBufControl : public hwreg::RegisterBase<DdiBufControl, uint32_t> {
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

// High byte of DDI_BUF_TRANS
class DdiBufTransHi : public hwreg::RegisterBase<DdiBufTransHi, uint32_t> {
public:
    DEF_FIELD(20, 16, vref);
    DEF_FIELD(10, 0, vswing);
};

// Low byte of DDI_BUF_TRANS
class DdiBufTransLo : public hwreg::RegisterBase<DdiBufTransLo, uint32_t> {
public:
    DEF_BIT(31, balance_leg_enable);
    DEF_FIELD(17, 0, deemphasis_level);
};

// DISPIO_CR_TX_BMU_CR0
class DisplayIoCtrlRegTxBmu : public hwreg::RegisterBase<DisplayIoCtrlRegTxBmu, uint32_t> {
public:
    DEF_FIELD(27, 23, disable_balance_leg);

    hwreg::BitfieldRef<uint32_t> tx_balance_leg_select(Ddi ddi) {
        int bit = 8 +  3 * ddi;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 2, bit);
    }

    static auto Get() { return hwreg::RegisterAddr<DisplayIoCtrlRegTxBmu>(0x6c00c); }
};

// DDI_AUX_CTL
class DdiAuxControl : public hwreg::RegisterBase<DdiAuxControl, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x64010;

    DEF_BIT(31, send_busy);
    DEF_BIT(30, done);
    DEF_BIT(29, interrupt_on_done);
    DEF_BIT(28, timeout);
    DEF_FIELD(27, 26, timeout_timer_value);
    DEF_BIT(25, rcv_error);
    DEF_FIELD(24, 20, message_size);
    DEF_FIELD(4, 0, sync_pulse_count);
};

// DDI_AUX_DATA
class DdiAuxData : public hwreg::RegisterBase<DdiAuxData, uint32_t> {
public:
    // There are 5 32-bit words at this register's address.
    static constexpr uint32_t kBaseAddr = 0x64014;
};

// DP_TP_CTL
class DdiDpTransportControl : public hwreg::RegisterBase<DdiDpTransportControl, uint32_t> {
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
    DdiRegs(Ddi ddi) : ddi_number_((int) ddi) { }

    hwreg::RegisterAddr<registers::DdiBufControl> DdiBufControl() {
        return GetReg<registers::DdiBufControl>();
    }
    hwreg::RegisterAddr<registers::DdiAuxControl> DdiAuxControl() {
        return GetReg<registers::DdiAuxControl>();
    }
    hwreg::RegisterAddr<registers::DdiAuxData> DdiAuxData() { return GetReg<registers::DdiAuxData>(); }
    hwreg::RegisterAddr<registers::DdiDpTransportControl> DdiDpTransportControl() {
        return GetReg<registers::DdiDpTransportControl>();
    }
    hwreg::RegisterAddr<registers::DdiBufTransHi> DdiBufTransHi(int index) {
        return hwreg::RegisterAddr<registers::DdiBufTransHi>(0x64e00 + 0x60 * ddi_number_ + 8 * index + 4);
    }
    hwreg::RegisterAddr<registers::DdiBufTransLo> DdiBufTransLo(int index) {
        return hwreg::RegisterAddr<registers::DdiBufTransLo>(0x64e00 + 0x60 * ddi_number_ + 8 * index);
    }

private:
    template <class RegType> hwreg::RegisterAddr<RegType> GetReg() {
        return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x100 * ddi_number_);
    }

    uint32_t ddi_number_;
};

} // namespace registers
