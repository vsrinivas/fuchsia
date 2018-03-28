// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Temporary Clock and PLL management until the clock protocol is fully
// developed.


#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>
#include <hw/reg.h>
#include <zircon/syscalls.h>

#include "aml-pcie-clk.h"

#define AXG_MIPI_CNTL0 0xa5b80000

#define PCIE_PLL_CNTL0 0x36
#define PCIE_PLL_CNTL1 0x37
#define PCIE_PLL_CNTL2 0x38
#define PCIE_PLL_CNTL3 0x39
#define PCIE_PLL_CNTL4 0x3A
#define PCIE_PLL_CNTL5 0x3B
#define PCIE_PLL_CNTL6 0x3C

#define AXG_PCIE_PLL_CNTL0 0x400106c8
#define AXG_PCIE_PLL_CNTL1 0x0084a2aa
#define AXG_PCIE_PLL_CNTL2 0xb75020be
#define AXG_PCIE_PLL_CNTL3 0x0a47488e
#define AXG_PCIE_PLL_CNTL4 0xc000004d
#define AXG_PCIE_PLL_CNTL5 0x00078000
#define AXG_PCIE_PLL_CNTL6 0x002323c6

#define MESON_PLL_ENABLE (1 << 30)
#define MESON_PLL_RESET  (1 << 29)

class MesonPLLControl0 : public hwreg::RegisterBase<MesonPLLControl0, uint32_t> {
  public:
    DEF_FIELD(8, 0, m);
    DEF_FIELD(13, 9, n);
    DEF_FIELD(17, 16, od);
    DEF_BIT(29, reset);
    DEF_BIT(30, enable);
    DEF_BIT(31, lock);

    static auto Get() {return hwreg::RegisterAddr<MesonPLLControl0>(0); }
};

class MesonPLLControl1 : public hwreg::RegisterBase<MesonPLLControl1, uint32_t> {
  public:
    DEF_FIELD(11, 0, div_frac);
    DEF_BIT(12, div_mode);
    DEF_FIELD(14, 13, dcvc_in);
    DEF_FIELD(16, 15, dco_sdmck_sel);
    DEF_BIT(17, dco_m_en);
    DEF_BIT(18, dco_band_opt);
    DEF_FIELD(21, 19, data_sel);
    DEF_FIELD(23, 22, afc_nt);
    DEF_FIELD(25, 24, afc_hold_t);
    DEF_FIELD(27, 26, afc_dsel_in);
    DEF_BIT(28, afc_dsel_bypass);
    DEF_BIT(29, afc_clk_sel);
    DEF_FIELD(31, 30, acq_r_ctr);

    static auto Get() {return hwreg::RegisterAddr<MesonPLLControl1>(0); }
};

class MesonPLLControl6 : public hwreg::RegisterBase<MesonPLLControl6, uint32_t> {
  public:
    DEF_FIELD(7, 6, od2);
    DEF_BIT(2, cml_input_sel1);
    DEF_BIT(1, cml_input_sel0);
    DEF_BIT(0, cml_input_en);

    static auto Get() { return hwreg::RegisterAddr<MesonPLLControl6>(0); }
};


zx_status_t pcie_pll_set_rate(zx_vaddr_t regbase) {
    // TODO(gkalsi): This statically configures the PCIe PLL to run at
    //               100mhz. When we write a real clock driver, we want this
    //               value to be configurable.

    volatile uint32_t* regs = (volatile uint32_t*)regbase;

    writel(AXG_MIPI_CNTL0, regs + 0);
    writel(AXG_PCIE_PLL_CNTL0, regs + PCIE_PLL_CNTL0);
    writel(AXG_PCIE_PLL_CNTL1, regs + PCIE_PLL_CNTL1);
    writel(AXG_PCIE_PLL_CNTL2, regs + PCIE_PLL_CNTL2);
    writel(AXG_PCIE_PLL_CNTL3, regs + PCIE_PLL_CNTL3);
    writel(AXG_PCIE_PLL_CNTL4, regs + PCIE_PLL_CNTL4);
    writel(AXG_PCIE_PLL_CNTL5, regs + PCIE_PLL_CNTL5);
    writel(AXG_PCIE_PLL_CNTL6, regs + PCIE_PLL_CNTL6);

    hwreg::RegisterIo cntl0_mmio(regs + PCIE_PLL_CNTL0);
    hwreg::RegisterIo cntl1_mmio(regs + PCIE_PLL_CNTL1);
    hwreg::RegisterIo cntl6_mmio(regs + PCIE_PLL_CNTL6);

    auto cntl0 = MesonPLLControl0::Get().ReadFrom(&cntl0_mmio);

    cntl0.set_enable(1);
    cntl0.set_reset(0);
    cntl0.WriteTo(&cntl0_mmio);

    cntl0.set_m(200);
    cntl0.set_n(3);
    cntl0.set_od(1);
    cntl0.WriteTo(&cntl0_mmio);

    auto cntl1 = MesonPLLControl1::Get().ReadFrom(&cntl1_mmio);
    cntl1.set_div_frac(0);
    cntl1.WriteTo(&cntl1_mmio);

    auto cntl6 = MesonPLLControl6::Get().ReadFrom(&cntl6_mmio);
    cntl6.set_od2(3);
    cntl6.set_cml_input_sel1(1);
    cntl6.set_cml_input_sel0(1);
    cntl6.set_cml_input_en(1);
    cntl6.WriteTo(&cntl6_mmio);

    // Assert the Reset pin on the PLL
    cntl0.set_reset(1);
    cntl0.WriteTo(&cntl0_mmio);

    // Wait for the reset to take effect
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    // De-assert the reset pin
    cntl0.set_reset(0);
    cntl0.WriteTo(&cntl0_mmio);

    // Wait for the PLL parameters to lock.
    const uint64_t kTimeout = 24000000;
    for (uint64_t attempts = 0; attempts < kTimeout; ++attempts) {
        auto cntl0 = MesonPLLControl0::Get().ReadFrom(&cntl0_mmio);

        // PLL has successfully locked?
        if (cntl0.lock()) return ZX_OK;
    }

    return ZX_ERR_TIMED_OUT;
}