// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/hdmi-display.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"

namespace i915_tgl {

namespace {

TEST(ComputeDpllConfigurationForHdmiTest, PrmExample1) {
  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", page 137.

  const uint32_t symbol_clock_khz = 113'309;
  uint16_t dco_int, dco_frac;
  uint8_t q, q_mode, k, p, cf;
  const bool success = ComputeDpllConfigurationForHdmi(symbol_clock_khz, &dco_int, &dco_frac, &q,
                                                       &q_mode, &k, &p, &cf);
  ASSERT_TRUE(success);

  EXPECT_EQ(377u, dco_int);
  EXPECT_EQ(22828u, dco_frac);
  EXPECT_EQ(4u, q);
  EXPECT_EQ(1u, q_mode);
  EXPECT_EQ(tgl_registers::DpllConfig2::kKdiv2, k);
  EXPECT_EQ(tgl_registers::DpllConfig2::kPdiv2, p);
  EXPECT_EQ(tgl_registers::DpllConfig2::k9000Mhz, cf);
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
  uint16_t dco_int, dco_frac;
  uint8_t q, q_mode, k, p, cf;
  const bool success = ComputeDpllConfigurationForHdmi(symbol_clock_khz, &dco_int, &dco_frac, &q,
                                                       &q_mode, &k, &p, &cf);
  ASSERT_TRUE(success);

  EXPECT_EQ(370u, dco_int);
  EXPECT_EQ(28794u, dco_frac);
  EXPECT_EQ(1u, q);
  EXPECT_EQ(0u, q_mode);
  EXPECT_EQ(tgl_registers::DpllConfig2::kKdiv3, k);
  EXPECT_EQ(tgl_registers::DpllConfig2::kPdiv2, p);
  EXPECT_EQ(tgl_registers::DpllConfig2::k9000Mhz, cf);
}

}  // namespace

}  // namespace i915_tgl
