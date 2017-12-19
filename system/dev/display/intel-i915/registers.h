// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include "registers-ddi.h"

namespace registers {

// Graphics & Memory Controller Hub Graphics Control - GGC_0_0_0_PCI
// This is a 16-bit register, so it needs to be populated manually
// TODO(stevensd/teisenbe): Is this true still?
class GmchGfxControl : public hwreg::RegisterBase<uint16_t> {
public:
    static constexpr uint32_t kAddr = 0x50;

    DEF_FIELD(7, 6, gtt_gfx_mem_size);

    static inline uint32_t mem_size_to_mb(uint32_t val) {
        return val ? 1 << (20 + val) : 0;
    }
};

// MASTER_INT_CTL
class MasterInterruptControl : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(31, enable_mask);
    DEF_BIT(23, sde_int_pending);

    static auto Get() { return hwreg::RegisterAddr<MasterInterruptControl>(0x44200); }
};

// GMBUS0
class GMBus0 : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_FIELD(2, 0, pin_pair_select);
    static constexpr uint32_t kDdiCPin = 4;
    static constexpr uint32_t kDdiBPin = 5;
    static constexpr uint32_t kDdiDPin = 6;

    static auto Get() { return hwreg::RegisterAddr<GMBus0>(0xc5100); }
};

// GMBUS1
class GMBus1 : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(31, sw_clear_int);
    DEF_BIT(30, sw_ready);
    DEF_BIT(27, bus_cycle_stop);
    DEF_BIT(25, bus_cycle_wait);
    DEF_FIELD(24, 16, total_byte_count);
    DEF_FIELD(7, 1, slave_register_index);
    DEF_BIT(0, read_op);

    static auto Get() { return hwreg::RegisterAddr<GMBus1>(0xc5104); }
};

// GMBUS2
class GMBus2 : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(11, hw_ready);
    DEF_BIT(10, nack);
    DEF_BIT(9, active);

    static auto Get() { return hwreg::RegisterAddr<GMBus2>(0xc5108); }
};

// GMBUS3
class GMBus3 : public hwreg::RegisterBase<uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<GMBus3>(0xc510c); }
};

// GMBUS4
class GMBus4 : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_FIELD(4, 0, interrupt_mask);

    static auto Get() { return hwreg::RegisterAddr<GMBus4>(0xc5110); }
};

// PWR_WELL_CTL
class PowerWellControl2 : public hwreg::RegisterBase<uint32_t> {
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
class FuseStatus : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(31, fuse_download_status);
    DEF_BIT(27, pg0_dist_status);
    DEF_BIT(26, pg1_dist_status);
    DEF_BIT(25, pg2_dist_status);

    static auto Get() { return hwreg::RegisterAddr<FuseStatus>(0x42000); }
};

// NDE_RSTWRN_OPT
class NorthDERestetWarning : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(4, rst_pch_handshake_enable);

    static auto Get() { return hwreg::RegisterAddr<NorthDERestetWarning>(0x46408); }
};

// CLCLK_CTL
class CdClockCtl : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_FIELD(27, 26, cd_freq_select);
    static constexpr uint32_t kFreqSelect3XX = 2;

    DEF_FIELD(10, 0, cd_freq_decimal);
    static constexpr uint32_t kFreqDecimal3375 = 0b01010100001;

    static auto Get() { return hwreg::RegisterAddr<CdClockCtl>(0x46000); }

};

// DBUF_CTL
class DbufCtl : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(31, power_request);
    DEF_BIT(31, power_state);

    static auto Get() { return hwreg::RegisterAddr<DbufCtl>(0x45008); }
};

// VGA_CONTROL
class VgaCtl : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(31, vga_display_disable);

    static auto Get() { return hwreg::RegisterAddr<VgaCtl>(0x41000); }
};

} // namespace registers
