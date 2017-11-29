// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "registers-base.h"
#include "registers-ddi.h"

namespace registers {

// DPLL_CTRL1
class DpllControl1 : public RegisterBase<DpllControl1> {
public:
    registers::BitfieldRef<uint32_t> dpll_hdmi_mode(int dpll) {
        int bit = dpll * 6 + 5;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    registers::BitfieldRef<uint32_t> dpll_ssc_enable(int dpll) {
        int bit = dpll * 6 + 4;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    registers::BitfieldRef<uint32_t> dpll_link_rate(int dpll) {
        int bit = dpll * 6 + 1;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 2, bit);
    }

    registers::BitfieldRef<uint32_t> dpll_override(int dpll) {
        int bit = dpll * 6;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static RegisterAddr<DpllControl1> Get() { return RegisterAddr<DpllControl1>(0x6c058); }
};

// DPLL_CTRL2
class DpllControl2 : public RegisterBase<DpllControl2> {
public:
    registers::BitfieldRef<uint32_t> ddi_clock_off(Ddi ddi) {
        int bit = 15 + ddi;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    registers::BitfieldRef<uint32_t> ddi_clock_select(Ddi ddi) {
        int bit = ddi * 3 + 1;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }

    registers::BitfieldRef<uint32_t> ddi_select_override(Ddi ddi) {
        int bit = ddi * 3;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static  RegisterAddr<DpllControl2> Get() { return RegisterAddr<DpllControl2>(0x6c05c); }
};

// DPLL_CFGCR1
class DpllConfig1 : public RegisterBase<DpllConfig1> {
public:
    DEF_BIT(31, frequency_enable);
    DEF_FIELD(23, 9, dco_fraction);
    DEF_FIELD(8, 0, dco_integer);

    static RegisterAddr<DpllConfig1> Get(int index) {
        assert(index == 1 || index == 2 || index == 3);
        return RegisterAddr<DpllConfig1>(0x6c040 + ((index - 1) * 8));
    }
};

// DPLL_CFGCR2
class DpllConfig2 : public RegisterBase<DpllConfig2> {
public:
    DEF_FIELD(15, 8, qdiv_ratio);
    DEF_BIT(7, qdiv_mode);

    DEF_FIELD(6, 5, kdiv_ratio);
    static constexpr uint8_t kKdiv5 = 0;
    static constexpr uint8_t kKdiv2 = 1;
    static constexpr uint8_t kKdiv3 = 2;
    static constexpr uint8_t kKdiv1 = 3;

    DEF_FIELD(4, 2, pdiv_ratio);
    static constexpr uint8_t kPdiv1 = 0;
    static constexpr uint8_t kPdiv2 = 1;
    static constexpr uint8_t kPdiv3 = 2;
    static constexpr uint8_t kPdiv7 = 4;

    DEF_FIELD(1, 0, central_freq);
    static constexpr uint8_t k9600Mhz = 0;
    static constexpr uint8_t k9000Mhz = 1;
    static constexpr uint8_t k8400Mhz = 3;

    static RegisterAddr<DpllConfig2> Get(int index) {
        return RegisterAddr<DpllConfig2>(0x6c044 + ((index - 1) * 8));
    }
};

// Virtual register which unifies the dpll enable bits (which are spread
// across 4 registers)
class DpllEnable : public RegisterBase<DpllEnable> {
public:
    DEF_BIT(31, enable_dpll);

    static RegisterAddr<DpllEnable> Get(int dpll) {
        if (dpll == 0) {
            return RegisterAddr<DpllEnable>(0x46010); // LCPLL1_CTL
        } else if (dpll == 1) {
            return RegisterAddr<DpllEnable>(0x46014); // LCPLL2_CTL
        } else if (dpll == 2) {
            return RegisterAddr<DpllEnable>(0x46040); // WRPLL_CTL1
        } else { // dpll == 3
            return RegisterAddr<DpllEnable>(0x46060); // WRPLL_CTL2
        }

    }
};

// DPLL_STATUS
class DpllStatus : public RegisterBase<DpllStatus> {
public:
    registers::BitfieldRef<uint32_t> dpll_lock(int dpll) {
        int bit = dpll * 8;
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static RegisterAddr<DpllStatus> Get() { return RegisterAddr<DpllStatus>(0x6c060); }
};

} // namespace registers
