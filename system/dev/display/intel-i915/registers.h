// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "registers-base.h"
#include "registers-ddi.h"

namespace registers {

// Graphics & Memory Controller Hub Graphics Control - GGC_0_0_0_PCI
// This is a 16-bit register, so it needs to be populated manually
class GmchGfxControl : public RegisterBase<GmchGfxControl> {
public:
    static constexpr uint32_t kAddr = 0x50;

    DEF_FIELD(7, 6, gtt_gfx_mem_size);

    static inline uint32_t mem_size_to_mb(uint32_t val) {
        return val ? 1 << (20 + val) : 0;
    }
};

// MASTER_INT_CTL
class MasterInterruptControl : public RegisterBase<MasterInterruptControl> {
public:
    DEF_BIT(31, enable_mask);
    DEF_BIT(23, sde_int_pending);

    static RegisterAddr<MasterInterruptControl> Get() {
        return RegisterAddr<MasterInterruptControl>(0x44200);
    }
};

// GMBUS0
class GMBus0 : public RegisterBase<GMBus0> {
public:
    DEF_FIELD(2, 0, pin_pair_select);
    static constexpr uint32_t kDdiCPin = 4;
    static constexpr uint32_t kDdiBPin = 5;
    static constexpr uint32_t kDdiDPin = 6;

    static RegisterAddr<GMBus0> Get() { return RegisterAddr<GMBus0>(0xc5100); }
};

// GMBUS1
class GMBus1 : public RegisterBase<GMBus1> {
public:
    DEF_BIT(31, sw_clear_int);
    DEF_BIT(30, sw_ready);
    DEF_BIT(27, bus_cycle_stop);
    DEF_BIT(25, bus_cycle_wait);
    DEF_FIELD(24, 16, total_byte_count);
    DEF_FIELD(7, 1, slave_register_index);
    DEF_BIT(0, read_op);

    static RegisterAddr<GMBus1> Get() { return RegisterAddr<GMBus1>(0xc5104); }
};

// GMBUS2
class GMBus2 : public RegisterBase<GMBus2> {
public:
    DEF_BIT(11, hw_ready);
    DEF_BIT(10, nack);
    DEF_BIT(9, active);

    static RegisterAddr<GMBus2> Get() { return RegisterAddr<GMBus2>(0xc5108); }
};

// GMBUS3
class GMBus3 : public RegisterBase<GMBus3> {
public:
    static RegisterAddr<GMBus3> Get() { return RegisterAddr<GMBus3>(0xc510c); }
};

// GMBUS4
class GMBus4 : public RegisterBase<GMBus4> {
public:
    DEF_FIELD(4, 0, interrupt_mask);

    static RegisterAddr<GMBus4> Get() { return RegisterAddr<GMBus4>(0xc5110); }
};

// PWR_WELL_CTL
class PowerWellControl2 : public RegisterBase<PowerWellControl2> {
public:
    DEF_BIT(31, power_well_2_request);
    DEF_BIT(30, power_well_2_state);
    DEF_BIT(29, power_well_1_request);
    DEF_BIT(28, power_well_1_state);
    DEF_BIT(1, misc_io_power_request);
    DEF_BIT(0, misc_io_power_state);

    registers::BitfieldRef<uint32_t> ddi_io_power_request(Ddi ddi) {
        int bit = 2 + ((ddi == DDI_A || ddi == DDI_E) ? 0 : ddi * 2) + 1;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    registers::BitfieldRef<uint32_t> ddi_io_power_state(Ddi ddi) {
        int bit = 2 + ((ddi == DDI_A || ddi == DDI_E) ? 0 : ddi * 2);
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static RegisterAddr<PowerWellControl2> Get() {
        return RegisterAddr<PowerWellControl2>(0x45404);
    }
};

// FUSE_STATUS
class FuseStatus : public RegisterBase<FuseStatus> {
public:
    DEF_BIT(31, fuse_download_status);
    DEF_BIT(27, pg0_dist_status);
    DEF_BIT(26, pg1_dist_status);
    DEF_BIT(25, pg2_dist_status);

    static RegisterAddr<FuseStatus> Get() { return RegisterAddr<FuseStatus>(0x42000); }
};

// NDE_RSTWRN_OPT
class NorthDERestetWarning : public RegisterBase<NorthDERestetWarning> {
public:
    DEF_BIT(4, rst_pch_handshake_enable);

    static RegisterAddr<NorthDERestetWarning> Get() {
        return RegisterAddr<NorthDERestetWarning>(0x46408);
    }
};

// CLCLK_CTL
class CdClockCtl : public RegisterBase<CdClockCtl> {
public:
    DEF_FIELD(27, 26, cd_freq_select);
    static constexpr uint32_t kFreqSelect3XX = 2;

    DEF_FIELD(10, 0, cd_freq_decimal);
    static constexpr uint32_t kFreqDecimal3375 = 0b01010100001;

    static RegisterAddr<CdClockCtl> Get() { return RegisterAddr<CdClockCtl>(0x46000); }

};

// DBUF_CTL
class DbufCtl : public RegisterBase<DbufCtl> {
public:
    DEF_BIT(31, power_request);
    DEF_BIT(31, power_state);

    static RegisterAddr<DbufCtl> Get() { return RegisterAddr<DbufCtl>(0x45008); }
};

// VGA_CONTROL
class VgaCtl : public RegisterBase<VgaCtl> {
public:
    DEF_BIT(31, vga_display_disable);

    static RegisterAddr<VgaCtl> Get() { return RegisterAddr<VgaCtl>(0x41000); }
};

} // namespace registers
