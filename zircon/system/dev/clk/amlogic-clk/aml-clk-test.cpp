// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-clk.h"
#include "aml-axg-blocks.h"

#include <ddk/platform-defs.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/aml-meson/axg-clk.h>
#include <soc/aml-s912/s912-hw.h>
#include <zxtest/zxtest.h>

namespace amlogic_clock {

class AmlClockTest : public AmlClock {
public:
    AmlClockTest(ddk_mock::MockMmioRegRegion& hiu, uint32_t did)
        : AmlClock(nullptr,
                   ddk::MmioBuffer(hiu.GetMmioBuffer()),
                   std::nullopt,
                   did) {}
};

TEST(ClkTestAml, AxgEnableDisableAll) {
    auto hiu_reg_arr = std::make_unique<ddk_mock::MockMmioReg[]>(S912_HIU_LENGTH);
    ddk_mock::MockMmioRegRegion hiu_regs(hiu_reg_arr.get(), sizeof(uint32_t),
                                         S912_HIU_LENGTH);

    AmlClockTest clk(hiu_regs, PDEV_DID_AMLOGIC_AXG_CLK);
    auto regvals = std::make_unique<uint32_t[]>(S912_HIU_LENGTH);

    constexpr size_t kClkStart = 0;
    constexpr size_t kClkEnd = static_cast<size_t>(CLK_AXG_COUNT);

    for (size_t i = kClkStart; i < kClkEnd; ++i) {
        const uint32_t reg = axg_clk_gates[i].reg;
        const uint32_t bit = (1u << axg_clk_gates[i].bit);
        regvals[reg] |= bit;
        hiu_regs[reg].ExpectWrite(regvals[reg]);

        const axg_clk_gate_idx clk_i = static_cast<axg_clk_gate_idx>(i);
        zx_status_t st = clk.ClockImplEnable(clk_i);
        EXPECT_OK(st);
    }

    for (size_t i = kClkStart; i < kClkEnd; ++i) {
        const uint32_t reg = axg_clk_gates[i].reg;
        const uint32_t bit = (1u << axg_clk_gates[i].bit);
        regvals[reg] &= ~(bit);
        hiu_regs[reg].ExpectWrite(regvals[reg]);

        const axg_clk_gate_idx clk_i = static_cast<axg_clk_gate_idx>(i);
        zx_status_t st = clk.ClockImplDisable(clk_i);
        EXPECT_OK(st);
    }

    hiu_regs.VerifyAll();
}

} // namespace amlogic_clock
