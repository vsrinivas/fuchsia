// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_DPLL_H
#define REGISTERS_DPLL_H

#include "register_bitfields.h"

// Registers for configuring the DPLLs (Display PLLs).
//
// The Intel graphics hardware provides 4 DPLLs:
//
//  * DPLL 0, also called "LCPLL 1": used for DDIs and for the CD clock
//    (Core Display clock)
//  * DPLL 1, also called "LCPLL 2": used for DDIs
//  * DPLL 2, also called "WRPLL 1": used for DDIs
//  * DPLL 3, also called "WRPLL 2": used for DDIs
//
// See the section "Display Engine PLLs" in
// intel-gfx-prm-osrc-skl-vol12-display.pdf.

namespace registers {

// DPLL_CTRL1
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DpllControl1 : public RegisterBase {
public:
    static constexpr int kLinkRate2700Mhz = 0; // DisplayPort 5.4 GHz
    static constexpr int kLinkRate1350Mhz = 1; // DisplayPort 2.7 GHz
    static constexpr int kLinkRate810Mhz = 2;  // DisplayPort 1.62 GHz
    static constexpr int kLinkRate1620Mhz = 3; // DisplayPort 3.24 GHz
    static constexpr int kLinkRate1080Mhz = 4; // DisplayPort 2.16 GHz
    static constexpr int kLinkRate2160Mhz = 5; // DisplayPort 4.32 GHz

    DEF_BIT(23, dpll3_hdmi_mode);
    DEF_BIT(22, dpll3_ssc_enable);
    DEF_FIELD(21, 19, dpll3_link_rate);
    DEF_BIT(18, dpll3_override);

    DEF_BIT(17, dpll2_hdmi_mode);
    DEF_BIT(16, dpll2_ssc_enable);
    DEF_FIELD(15, 13, dpll2_link_rate);
    DEF_BIT(12, dpll2_override);

    DEF_BIT(11, dpll1_hdmi_mode);
    DEF_BIT(10, dpll1_ssc_enable);
    DEF_FIELD(9, 7, dpll1_link_rate);
    DEF_BIT(6, dpll1_override);

    DEF_BIT(5, dpll0_hdmi_mode);
    DEF_BIT(4, dpll0_ssc_enable);
    DEF_FIELD(3, 1, dpll0_link_rate);
    DEF_BIT(0, dpll0_override);

    static auto Get() { return RegisterAddr<DpllControl1>(0x6c058); }
};

// DPLL_CTRL2
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class DpllControl2 : public RegisterBase {
public:
    DEF_BIT(19, ddi_e_clock_off);
    DEF_BIT(18, ddi_d_clock_off);
    DEF_BIT(17, ddi_c_clock_off);
    DEF_BIT(16, ddi_b_clock_off);
    DEF_BIT(15, ddi_a_clock_off);
    DEF_FIELD(14, 13, ddi_e_clock_select);
    DEF_BIT(12, ddi_e_select_override);
    DEF_FIELD(11, 10, ddi_d_clock_select);
    DEF_BIT(9, ddi_d_select_override);
    DEF_FIELD(8, 7, ddi_c_clock_select);
    DEF_BIT(6, ddi_c_select_override);
    DEF_FIELD(5, 4, ddi_b_clock_select);
    DEF_BIT(3, ddi_b_select_override);
    DEF_FIELD(2, 1, ddi_a_clock_select);
    DEF_BIT(0, ddi_a_select_override);

    static auto Get() { return RegisterAddr<DpllControl2>(0x6c05c); }
};

// LCPLL2_CTL
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf
class Lcpll2Control : public RegisterBase {
public:
    DEF_BIT(31, enable_dpll1);

    static auto Get() { return RegisterAddr<Lcpll2Control>(0x46014); }
};

} // namespace

#endif // REGISTERS_DPLL_H
