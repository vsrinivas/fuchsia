// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <hwreg/bitfields.h>
#include "registers-ddi.h"

namespace registers {

static constexpr uint32_t kDpllCount = 4;

enum Dpll {
    DPLL_INVALID = -1,
    DPLL_0 = 0,
    DPLL_1,
    DPLL_2,
    DPLL_3,
};

static const Dpll kDplls[kDpllCount] = {
    DPLL_0, DPLL_1, DPLL_2, DPLL_3,
};

// DPLL_CTRL1
class DpllControl1 : public hwreg::RegisterBase<DpllControl1, uint32_t> {
public:
    hwreg::BitfieldRef<uint32_t> dpll_hdmi_mode(Dpll dpll) {
        int bit = dpll * 6 + 5;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> dpll_ssc_enable(Dpll dpll) {
        int bit = dpll * 6 + 4;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> dpll_link_rate(Dpll dpll) {
        int bit = dpll * 6 + 1;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 2, bit);
    }
    static constexpr int kLinkRate2700Mhz = 0; // DisplayPort 5.4 GHz
    static constexpr int kLinkRate1350Mhz = 1; // DisplayPort 2.7 GHz
    static constexpr int kLinkRate810Mhz = 2;  // DisplayPort 1.62 GHz
    static constexpr int kLinkRate1620Mhz = 3; // DisplayPort 3.24 GHz
    static constexpr int kLinkRate1080Mhz = 4; // DisplayPort 2.16 GHz
    static constexpr int kLinkRate2160Mhz = 5; // DisplayPort 4.32 GHz

    hwreg::BitfieldRef<uint32_t> dpll_override(Dpll dpll) {
        int bit = dpll * 6;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static auto Get() { return hwreg::RegisterAddr<DpllControl1>(0x6c058); }
};

// DPLL_CTRL2
class DpllControl2 : public hwreg::RegisterBase<DpllControl2, uint32_t> {
public:
    hwreg::BitfieldRef<uint32_t> ddi_clock_off(Ddi ddi) {
        int bit = 15 + ddi;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> ddi_clock_select(Ddi ddi) {
        int bit = ddi * 3 + 1;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }

    hwreg::BitfieldRef<uint32_t> ddi_select_override(Ddi ddi) {
        int bit = ddi * 3;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static auto Get() { return hwreg::RegisterAddr<DpllControl2>(0x6c05c); }
};

// DPLL_CFGCR1
class DpllConfig1 : public hwreg::RegisterBase<DpllConfig1, uint32_t> {
public:
    DEF_BIT(31, frequency_enable);
    DEF_FIELD(23, 9, dco_fraction);
    DEF_FIELD(8, 0, dco_integer);

    static auto Get(Dpll dpll) {
        ZX_ASSERT(dpll == DPLL_1 || dpll == DPLL_2 || dpll == DPLL_3);
        return hwreg::RegisterAddr<DpllConfig1>(0x6c040 + ((dpll - 1) * 8));
    }
};

// DPLL_CFGCR2
class DpllConfig2 : public hwreg::RegisterBase<DpllConfig2, uint32_t> {
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

    static auto Get(int dpll) {
        ZX_ASSERT(dpll == DPLL_1 || dpll == DPLL_2 || dpll == DPLL_3);
        return hwreg::RegisterAddr<DpllConfig2>(0x6c044 + ((dpll - 1) * 8));
    }
};

// Virtual register which unifies the dpll enable bits (which are spread
// across 4 registers)
class DpllEnable : public hwreg::RegisterBase<DpllEnable, uint32_t> {
public:
    DEF_BIT(31, enable_dpll);

    static auto Get(Dpll dpll) {
        if (dpll == 0) {
            return hwreg::RegisterAddr<DpllEnable>(0x46010); // LCPLL1_CTL
        } else if (dpll == 1) {
            return hwreg::RegisterAddr<DpllEnable>(0x46014); // LCPLL2_CTL
        } else if (dpll == 2) {
            return hwreg::RegisterAddr<DpllEnable>(0x46040); // WRPLL_CTL1
        } else { // dpll == 3
            return hwreg::RegisterAddr<DpllEnable>(0x46060); // WRPLL_CTL2
        }

    }
};

// DPLL_STATUS
class DpllStatus : public hwreg::RegisterBase<DpllStatus, uint32_t> {
public:
    hwreg::BitfieldRef<uint32_t> dpll_lock(Dpll dpll) {
        int bit = dpll * 8;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    static auto Get() { return hwreg::RegisterAddr<DpllStatus>(0x6c060); }
};

// LCPLL1_CTL
class Lcpll1Control : public hwreg::RegisterBase<Lcpll1Control, uint32_t> {
public:
    DEF_BIT(30, pll_lock);

    static auto Get() { return hwreg::RegisterAddr<Lcpll1Control>(0x46010); }
};

} // namespace registers
