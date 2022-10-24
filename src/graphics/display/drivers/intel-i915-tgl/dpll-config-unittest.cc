// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/dpll-config.h"

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"

namespace i915_tgl {

namespace {

TEST(CreateDpllFrequencyDividerConfigKabyLakeTest, AllDivisors) {
  for (const int8_t& divider : DpllSupportedFrequencyDividersKabyLake()) {
    SCOPED_TRACE(testing::Message() << "Divider: " << int{divider});

    const DpllFrequencyDividerConfig divider_config =
        CreateDpllFrequencyDividerConfigKabyLake(divider);
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

TEST(CreateDpllFrequencyDividerConfigKabyLakeTest, PrmExample1) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", page 137.

  const DpllFrequencyDividerConfig divider_config = CreateDpllFrequencyDividerConfigKabyLake(16);
  EXPECT_EQ(2, divider_config.p0_p_divider);
  EXPECT_EQ(4, divider_config.p1_q_divider);
  EXPECT_EQ(2, divider_config.p2_k_divider);
}

TEST(CreateDpllFrequencyDividerConfigKabyLakeTest, PrmExample2) {
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

  const DpllFrequencyDividerConfig divider_config = CreateDpllFrequencyDividerConfigKabyLake(6);
  EXPECT_EQ(2, divider_config.p0_p_divider);
  EXPECT_EQ(1, divider_config.p1_q_divider);
  EXPECT_EQ(3, divider_config.p2_k_divider);
}

TEST(CreateDpllFrequencyDividerConfigTigerLakeTest, AllDivisors) {
  for (const int8_t& divider : DpllSupportedFrequencyDividersTigerLake()) {
    SCOPED_TRACE(testing::Message() << "Divider: " << int{divider});

    const DpllFrequencyDividerConfig divider_config =
        CreateDpllFrequencyDividerConfigTigerLake(divider);
    EXPECT_EQ(divider, divider_config.p0_p_divider * divider_config.p1_q_divider *
                           divider_config.p2_k_divider);

    EXPECT_GT(divider_config.p0_p_divider, 0);
    EXPECT_GT(divider_config.p1_q_divider, 0);
    EXPECT_GT(divider_config.p2_k_divider, 0);

    // The helpers below ZX_DEBUG_ASSERT() on incorrect divider values. The
    // assignments are on separate lines to facilitate debugging.
    auto pll_dco_dividers =
        tgl_registers::DisplayPllDcoDividersTigerLake::GetForDpll(tgl_registers::DPLL_0)
            .FromValue(0);
    pll_dco_dividers.set_p_p0_divider(static_cast<uint8_t>(divider_config.p0_p_divider));
    pll_dco_dividers.set_q_p1_divider(static_cast<uint8_t>(divider_config.p1_q_divider));
    pll_dco_dividers.set_k_p2_divider(static_cast<uint8_t>(divider_config.p2_k_divider));
  }
}

TEST(CreateDpllFrequencyDividerConfigTigerLakeTest, PrmExample1) {
  // Values from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "Example of DVI on DDIB
  // using 113.309 MHz symbol clock and reference 24 MHz", page 182.

  const DpllFrequencyDividerConfig divider_config = CreateDpllFrequencyDividerConfigTigerLake(16);
  EXPECT_EQ(2, divider_config.p0_p_divider);
  EXPECT_EQ(4, divider_config.p1_q_divider);
  EXPECT_EQ(2, divider_config.p2_k_divider);
}

TEST(CreateDpllFrequencyDividerConfigTigerLakeTest, PrmExample2) {
  // Values from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "Example for DSI0 8X
  // 556.545 and reference 24 MHz", pages 185-186.

  const DpllFrequencyDividerConfig divider_config = CreateDpllFrequencyDividerConfigTigerLake(3);
  EXPECT_EQ(3, divider_config.p0_p_divider);
  EXPECT_EQ(1, divider_config.p1_q_divider);
  EXPECT_EQ(1, divider_config.p2_k_divider);
}

TEST(CreateDpllFrequencyDividerConfigTigerLakeTest, DisplayPortTable) {
  // Test cases from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "DisplayPort Mode PLL
  // Values" pages 178-179.

  struct TableRow {
    int8_t p, k, q;
  };
  static constexpr TableRow kTableRows[] = {
      {.p = 3, .k = 1, .q = 1}, {.p = 3, .k = 2, .q = 1}, {.p = 5, .k = 2, .q = 1},
      {.p = 5, .k = 1, .q = 1}, {.p = 2, .k = 2, .q = 2}, {.p = 2, .k = 2, .q = 1},
      {.p = 2, .k = 1, .q = 1},
  };

  for (const TableRow& test_row : kTableRows) {
    const int8_t divider = static_cast<int8_t>(int{test_row.p} * int{test_row.k} * int{test_row.q});
    SCOPED_TRACE(testing::Message() << "Divider: " << int{divider});

    const DpllFrequencyDividerConfig divider_config =
        CreateDpllFrequencyDividerConfigTigerLake(divider);
    EXPECT_EQ(test_row.p, divider_config.p0_p_divider);
    EXPECT_EQ(test_row.k, divider_config.p2_k_divider);
    EXPECT_EQ(test_row.q, divider_config.p1_q_divider);
  }
}

TEST(CreateDpllOscillatorConfigKabyLakeTest, PrmExample1) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", page 137.

  const DpllOscillatorConfig dco_config = CreateDpllOscillatorConfigKabyLake(113'309 * 5);
  EXPECT_EQ(9'000'000, dco_config.center_frequency_khz);
  EXPECT_EQ(16, dco_config.frequency_divider);
  EXPECT_EQ(113'309 * 5 * 16, dco_config.frequency_khz);
}

TEST(CreateDpllOscillatorConfigKabyLakeTest, PrmExample2) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of HDMI on DDIC using
  // 296.703 MHz symbol clock", pages 137-138.

  const DpllOscillatorConfig dco_config = CreateDpllOscillatorConfigKabyLake(296'703 * 5);
  EXPECT_EQ(9'000'000, dco_config.center_frequency_khz);
  EXPECT_EQ(6, dco_config.frequency_divider);
  EXPECT_EQ(296'703 * 5 * 6, dco_config.frequency_khz);
}

TEST(CreateDpllOscillatorConfigForHdmiTigerLakeTest, PrmExample1) {
  // Values from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "Example of DVI on DDIB
  // using 113.309 MHz symbol clock and reference 24 MHz", page 182.

  const DpllOscillatorConfig dco_config = CreateDpllOscillatorConfigForHdmiTigerLake(113'309 * 5);
  EXPECT_EQ(8'999'000, dco_config.center_frequency_khz);
  EXPECT_EQ(16, dco_config.frequency_divider);
  EXPECT_EQ(9'064'720, dco_config.frequency_khz);
}

TEST(CreateDpllOscillatorConfigForHdmiTigerLakeTest, PrmExample2) {
  // Values from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "Example for DSI0 8X
  // 556.545 and reference 24 MHz", pages 185-186.

  const DpllOscillatorConfig dco_config = CreateDpllOscillatorConfigForHdmiTigerLake(566'545 * 5);
  EXPECT_EQ(8'999'000, dco_config.center_frequency_khz);
  EXPECT_EQ(3, dco_config.frequency_divider);
  EXPECT_EQ(8'498'175, dco_config.frequency_khz);
}

TEST(CreateDpllOscillatorConfigForHdmiTigerLakeTest, DisplayPortTable) {
  // Test cases from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "DisplayPort Mode PLL
  // Values" pages 178-179.

  struct TableRow {
    int32_t link_rate;
    int32_t frequency;
    int8_t divider;
  };
  static constexpr TableRow kTableRows[] = {
      // The algorithm solutions match the table for the cases below.
      {.link_rate = 5'400'000, .frequency = 8'100'000, .divider = 3},
      {.link_rate = 2'160'000, .frequency = 8'640'000, .divider = 8},
      {.link_rate = 4'320'000, .frequency = 8'640'000, .divider = 4},
      {.link_rate = 6'480'000, .frequency = 9'720'000, .divider = 3},
      {.link_rate = 8'100'000, .frequency = 8'100'000, .divider = 2},

      // The algorithm finds different values from the table. The solutions
      // here are better than the table's solutions in respect to the
      // algorithm's stated goal of minimizing DCO frequency deviation from the
      // centrer frequency.
      {.link_rate = 2'700'000, .frequency = 9'450'000, .divider = 7},
      {.link_rate = 1'620'000, .frequency = 9'720'000, .divider = 12},
      {.link_rate = 3'240'000, .frequency = 9'720'000, .divider = 6},
  };

  for (const TableRow& test_row : kTableRows) {
    const int32_t afe_clock_khz = static_cast<int32_t>(test_row.link_rate / 2);
    SCOPED_TRACE(testing::Message()
                 << "Link rate: " << test_row.link_rate << " kHz AFE clock: " << afe_clock_khz);

    const DpllOscillatorConfig dco_config =
        CreateDpllOscillatorConfigForHdmiTigerLake(afe_clock_khz);
    EXPECT_EQ(8'999'000, dco_config.center_frequency_khz);
    EXPECT_EQ(test_row.frequency, dco_config.frequency_khz);
    EXPECT_EQ(test_row.divider, dco_config.frequency_divider);
  }
}

TEST(CreateDpllOscillatorConfigForDisplayPortTigerLakeTest, DisplayPortTable) {
  // Test cases from IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "DisplayPort Mode PLL
  // Values" pages 178-179.

  struct TableRow {
    int32_t link_rate;
    int32_t frequency;
    int8_t divider;
  };
  static constexpr TableRow kTableRows[] = {
      // The algorithm solutions match the table for the cases below.
      {.link_rate = 5'400'000, .frequency = 8'100'000, .divider = 3},
      {.link_rate = 2'700'000, .frequency = 8'100'000, .divider = 6},
      {.link_rate = 1'620'000, .frequency = 8'100'000, .divider = 10},
      {.link_rate = 3'240'000, .frequency = 8'100'000, .divider = 5},
      {.link_rate = 2'160'000, .frequency = 8'640'000, .divider = 8},
      {.link_rate = 4'320'000, .frequency = 8'640'000, .divider = 4},
      {.link_rate = 6'480'000, .frequency = 9'720'000, .divider = 3},
      {.link_rate = 8'100'000, .frequency = 8'100'000, .divider = 2},
  };

  for (const TableRow& test_row : kTableRows) {
    const int32_t afe_clock_khz = static_cast<int32_t>(test_row.link_rate / 2);
    SCOPED_TRACE(testing::Message()
                 << "Link rate: " << test_row.link_rate << " kHz AFE clock: " << afe_clock_khz);

    const DpllOscillatorConfig dco_config =
        CreateDpllOscillatorConfigForDisplayPortTigerLake(afe_clock_khz);
    EXPECT_EQ(8'999'000, dco_config.center_frequency_khz);
    EXPECT_EQ(test_row.frequency, dco_config.frequency_khz);
    EXPECT_EQ(test_row.divider, dco_config.frequency_divider);
  }
}

}  // namespace

}  // namespace i915_tgl
