// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/hdmi-display.h"

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"

namespace i915_tgl {

namespace {

TEST(CreateDpllFrequencyDividerConfigTest, AllDivisors) {
  for (const int8_t& divider : DpllSupportedFrequencyDividers()) {
    SCOPED_TRACE(testing::Message() << "Divider: " << int{divider});

    const DpllFrequencyDividerConfig divider_config = CreateDpllFrequencyDividerConfig(divider);
    EXPECT_EQ(divider, divider_config.p0_p_divider * divider_config.p1_q_divider *
                           divider_config.p2_k_divider);

    EXPECT_GT(divider_config.p0_p_divider, 0);
    EXPECT_GT(divider_config.p1_q_divider, 0);
    EXPECT_GT(divider_config.p2_k_divider, 0);

    // The helpers below ZX_DEBUG_ASSERT() on incorrect divider values. The
    // assignments are on separate lines to facilitate debugging.
    auto dpll_config2 =
        tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::DPLL_1)
            .FromValue(0);
    dpll_config2.set_p_p0_divider(static_cast<uint8_t>(divider_config.p0_p_divider));
    dpll_config2.set_q_p1_divider(static_cast<uint8_t>(divider_config.p1_q_divider));
    dpll_config2.set_k_p2_divider(static_cast<uint8_t>(divider_config.p2_k_divider));
  }
}

TEST(CreateDpllOscillatorConfigForFrequencyTest, PrmExample1) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", page 137.

  const DpllOscillatorConfig dco_config = CreateDpllOscillatorConfig(113'309 * 5);
  EXPECT_EQ(9'000'000, dco_config.center_frequency_khz);
  EXPECT_EQ(16, dco_config.frequency_divider);
  EXPECT_EQ(113'309 * 5 * 16, dco_config.frequency_khz);
}

TEST(CreateDpllOscillatorConfigForFrequencyTest, PrmExample2) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of HDMI on DDIC using
  // 296.703 MHz symbol clock", pages 137-138.

  const DpllOscillatorConfig dco_config = CreateDpllOscillatorConfig(296'703 * 5);
  EXPECT_EQ(9'000'000, dco_config.center_frequency_khz);
  EXPECT_EQ(6, dco_config.frequency_divider);
  EXPECT_EQ(296'703 * 5 * 6, dco_config.frequency_khz);
}

TEST(ComputeDpllConfigurationForHdmiTest, PrmExample1) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", page 137.

  const uint32_t symbol_clock_khz = 113'309;
  std::optional<HdmiDpllState> result = ComputeDpllConfigurationForHdmi(symbol_clock_khz);
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(9'064'720, result->dco_frequency_khz);
  EXPECT_EQ(4u, result->q_divider);
  EXPECT_EQ(2u, result->k_divider);
  EXPECT_EQ(2u, result->p_divider);
  EXPECT_EQ(9'000, result->dco_center_frequency_mhz);
}

TEST(ComputeDpllConfigurationForHdmiTest, PrmExample2) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of HDMI on DDIC using
  // 296.703 MHz symbol clock", pages 137-138.
  //
  // The K (P2) and P (P0) divisor values don't match the PRM values. The PRM
  // states "P0 = 1, P1 = 3, P2 = 2" in the summary, and then "P1 = 1",
  // "Kdiv = P2 = 01b (2)", "Pdiv = P0 = 010b (3)" in the DPLL2_CFGCR2
  // breakdown.
  //
  // The getMultiplier(num) pseudocode produces P0 = 2, P1 = 1, P2 = 3 because
  // num % 2 == 0 and num1 (in the first if branch) == 3. The pseudocode matches
  // the OpenBSD i915 driver code.

  const uint32_t symbol_clock_khz = 296'703;
  std::optional<HdmiDpllState> result = ComputeDpllConfigurationForHdmi(symbol_clock_khz);
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(8'901'090, result->dco_frequency_khz);
  EXPECT_EQ(9'000, result->dco_center_frequency_mhz);
  EXPECT_EQ(1u, result->q_divider);
  EXPECT_EQ(3u, result->k_divider);
  EXPECT_EQ(2u, result->p_divider);
}

}  // namespace

}  // namespace i915_tgl
