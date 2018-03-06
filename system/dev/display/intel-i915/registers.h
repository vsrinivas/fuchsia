// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/assert.h>

#include "registers-ddi.h"

namespace registers {

// Graphics & Memory Controller Hub Graphics Control - GGC_0_0_0_PCI
class GmchGfxControl : public hwreg::RegisterBase<GmchGfxControl, uint16_t> {
public:
    static constexpr uint32_t kAddr = 0x50; // Address for the mirror

    DEF_FIELD(15, 8, gfx_mode_select);
    DEF_FIELD(7, 6, gtt_size);

    inline uint32_t gtt_mappable_mem_size() {
        return gtt_size() ? 1 << (20 + gtt_size()) : 0;
    }

    inline uint32_t dsm_size() {
        if (gfx_mode_select() <= 0x10) {
            return gfx_mode_select() * 32 * 1024 * 1024;
        } else if (gfx_mode_select() == 0x20) {
            return 1024 * 1024 * 1024;
        } else if (gfx_mode_select() == 0x30) {
            return 1536 * 1024 * 1024;
        } else if (gfx_mode_select() == 0x40) {
            return 2048 * 1024 * 1024u;
        } else if (0xf0 <= gfx_mode_select() && gfx_mode_select() < 0xff) {
            return (gfx_mode_select() - 0xef) * 4 * 1024 * 1024;
        } else {
            return 0;
        }
    }

    static auto Get() { return hwreg::RegisterAddr<GmchGfxControl>(0); }
};

// Base Data of Stolen Memory - BDSM_0_0_0_PCI
class BaseDsm : public hwreg::RegisterBase<BaseDsm, uint32_t> {
public:
    static constexpr uint32_t kAddr = 0x5c; // Address for the mirror

    DEF_FIELD(31, 20, base_phys_addr);
    static constexpr uint32_t base_phys_addr_shift = 20;
    DEF_RSVDZ_FIELD(19, 1);
    DEF_BIT(0, lock);

    static auto Get() { return hwreg::RegisterAddr<BaseDsm>(0); }
};

// MASTER_INT_CTL
class MasterInterruptControl : public hwreg::RegisterBase<MasterInterruptControl, uint32_t> {
public:
    DEF_BIT(31, enable_mask);
    DEF_BIT(23, sde_int_pending);
    DEF_BIT(18, de_pipe_c_int_pending);
    DEF_BIT(17, de_pipe_b_int_pending);
    DEF_BIT(16, de_pipe_a_int_pending);

    static auto Get() { return hwreg::RegisterAddr<MasterInterruptControl>(0x44200); }
};

// GMBUS0
class GMBus0 : public hwreg::RegisterBase<GMBus0, uint32_t> {
public:
    DEF_FIELD(2, 0, pin_pair_select);
    static constexpr uint32_t kDdiCPin = 4;
    static constexpr uint32_t kDdiBPin = 5;
    static constexpr uint32_t kDdiDPin = 6;

    static auto Get() { return hwreg::RegisterAddr<GMBus0>(0xc5100); }
};

// GMBUS1
class GMBus1 : public hwreg::RegisterBase<GMBus1, uint32_t> {
public:
    DEF_BIT(31, sw_clear_int);
    DEF_BIT(30, sw_ready);
    DEF_BIT(27, bus_cycle_stop);
    DEF_BIT(25, bus_cycle_wait);
    DEF_FIELD(24, 16, total_byte_count);
    DEF_FIELD(7, 1, slave_register_addr);
    DEF_BIT(0, read_op);

    static auto Get() { return hwreg::RegisterAddr<GMBus1>(0xc5104); }
};

// GMBUS2
class GMBus2 : public hwreg::RegisterBase<GMBus2, uint32_t> {
public:
    DEF_BIT(14, wait);
    DEF_BIT(11, hw_ready);
    DEF_BIT(10, nack);
    DEF_BIT(9, active);

    static auto Get() { return hwreg::RegisterAddr<GMBus2>(0xc5108); }
};

// GMBUS3
class GMBus3 : public hwreg::RegisterBase<GMBus3, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<GMBus3>(0xc510c); }
};

// GMBUS4
class GMBus4 : public hwreg::RegisterBase<GMBus4, uint32_t> {
public:
    DEF_FIELD(4, 0, interrupt_mask);

    static auto Get() { return hwreg::RegisterAddr<GMBus4>(0xc5110); }
};

// PWR_WELL_CTL
class PowerWellControl2 : public hwreg::RegisterBase<PowerWellControl2, uint32_t> {
public:
    DEF_BIT(31, power_well_2_request);
    DEF_BIT(30, power_well_2_state);
    DEF_BIT(29, power_well_1_request);
    DEF_BIT(28, power_well_1_state);
    DEF_BIT(1, misc_io_power_request);
    DEF_BIT(0, misc_io_power_state);

    hwreg::BitfieldRef<uint32_t> ddi_io_power_request(Ddi ddi) {
        int bit = 2 + ((ddi == DDI_A || ddi == DDI_E) ? 0 : ddi * 2) + 1;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> ddi_io_power_state(Ddi ddi) {
        int bit = 2 + ((ddi == DDI_A || ddi == DDI_E) ? 0 : ddi * 2);
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static auto Get() { return hwreg::RegisterAddr<PowerWellControl2>(0x45404); }
};

// FUSE_STATUS
class FuseStatus : public hwreg::RegisterBase<FuseStatus, uint32_t> {
public:
    DEF_BIT(31, fuse_download_status);
    DEF_BIT(27, pg0_dist_status);
    DEF_BIT(26, pg1_dist_status);
    DEF_BIT(25, pg2_dist_status);

    static auto Get() { return hwreg::RegisterAddr<FuseStatus>(0x42000); }
};

// NDE_RSTWRN_OPT
class NorthDERestetWarning : public hwreg::RegisterBase<NorthDERestetWarning, uint32_t> {
public:
    DEF_BIT(4, rst_pch_handshake_enable);

    static auto Get() { return hwreg::RegisterAddr<NorthDERestetWarning>(0x46408); }
};

// CLCLK_CTL
class CdClockCtl : public hwreg::RegisterBase<CdClockCtl, uint32_t> {
public:
    DEF_FIELD(27, 26, cd_freq_select);
    static constexpr uint32_t kFreqSelect3XX = 2;

    DEF_FIELD(10, 0, cd_freq_decimal);
    static constexpr uint32_t kFreqDecimal3375 = 0b01010100001;

    static auto Get() { return hwreg::RegisterAddr<CdClockCtl>(0x46000); }

};

// DBUF_CTL
class DbufCtl : public hwreg::RegisterBase<DbufCtl, uint32_t> {
public:
    DEF_BIT(31, power_request);
    DEF_BIT(30, power_state);

    static auto Get() { return hwreg::RegisterAddr<DbufCtl>(0x45008); }
};

// VGA_CONTROL
class VgaCtl : public hwreg::RegisterBase<VgaCtl, uint32_t> {
public:
    DEF_BIT(31, vga_display_disable);

    static auto Get() { return hwreg::RegisterAddr<VgaCtl>(0x41000); }
};

// GPIO_CTL
class GpioCtl : public hwreg::RegisterBase<GpioCtl, uint32_t> {
public:
    DEF_BIT(12, data_in);
    DEF_BIT(11, data_out);
    DEF_BIT(10, data_mask);
    DEF_BIT(9, data_direction_val);
    DEF_BIT(8, data_direction_mask);

    DEF_BIT(4, clock_in);
    DEF_BIT(3, clock_out);
    DEF_BIT(2, clock_mask);
    DEF_BIT(1, clock_direction_val);
    DEF_BIT(0, clock_direction_mask);

    static auto Get(registers::Ddi ddi) {
        if (ddi == registers::DDI_B) {
            return hwreg::RegisterAddr<GpioCtl>(0xc5020);
        } else if (ddi == registers::DDI_C) {
            return hwreg::RegisterAddr<GpioCtl>(0xc501c);
        } else { // ddi == registers::DDI_D
            ZX_DEBUG_ASSERT(ddi == registers::DDI_D);
            return hwreg::RegisterAddr<GpioCtl>(0xc5024);
        }
    }
};

// SBLC_PWM_CTL1
class SouthBacklightCtl1 : public hwreg::RegisterBase<SouthBacklightCtl1, uint32_t> {
public:
    DEF_BIT(31, enable);
    DEF_RSVDZ_BIT(30);
    DEF_BIT(29, polarity);
    DEF_RSVDZ_FIELD(28, 0);

    static auto Get() { return hwreg::RegisterAddr<SouthBacklightCtl1>(0xc8250); }
};

// SBLC_PWM_CTL2
class SouthBacklightCtl2 : public hwreg::RegisterBase<SouthBacklightCtl2, uint32_t> {
public:
    DEF_FIELD(31, 16, modulation_freq);
    DEF_FIELD(15, 0, duty_cycle);

    static auto Get() { return hwreg::RegisterAddr<SouthBacklightCtl2>(0xc8254); }
};

// SCHICKEN_1
class SChicken1 : public hwreg::RegisterBase<SChicken1, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<SChicken1>(0xc2000); }
};

// PP_CONTROL
class PanelPowerCtrl : public hwreg::RegisterBase<PanelPowerCtrl, uint32_t> {
public:
    DEF_RSVDZ_FIELD(15, 4);
    DEF_BIT(3, vdd_override);
    DEF_BIT(2, backlight_enable);
    DEF_BIT(1, pwr_down_on_reset);
    DEF_BIT(0, power_state_target);

    static auto Get() { return hwreg::RegisterAddr<PanelPowerCtrl>(0xc7204); }
};

// PP_DIVISOR
class PanelPowerDivisor : public hwreg::RegisterBase<PanelPowerDivisor, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<PanelPowerDivisor>(0xc7210); }
};

// PP_OFF_DELAYS
class PanelPowerOffDelay: public hwreg::RegisterBase<PanelPowerOffDelay, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<PanelPowerOffDelay>(0xc720c); }
};

// PP_ON_DELAYS
class PanelPowerOnDelay: public hwreg::RegisterBase<PanelPowerOnDelay, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<PanelPowerOnDelay>(0xc7208); }
};

// PP_STATUS
class PanelPowerStatus: public hwreg::RegisterBase<PanelPowerStatus, uint32_t> {
public:
    DEF_BIT(31, on_status);
    DEF_RSVDZ_BIT(30);
    DEF_FIELD(29, 28, pwr_seq_progress);
    static constexpr uint32_t kPrwSeqNone = 0;
    static constexpr uint32_t kPrwSeqPwrUp = 1;
    static constexpr uint32_t kPrwSeqPwrDown = 2;
    DEF_BIT(27, pwr_cycle_delay_active);
    DEF_RSVDZ_FIELD(26, 4);

    static auto Get() { return hwreg::RegisterAddr<PanelPowerStatus>(0xc7200); }
};

} // namespace registers
