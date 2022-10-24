// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace i915_tgl {

namespace {

TEST(DisplayPllControl1Test, PllUsesHdmiConfigurationMode) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  dpll_control1.set_reg_value(0).set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_1,
                                                                      true);
  EXPECT_EQ(true, dpll_control1.pll1_uses_hdmi_configuration_mode());
  EXPECT_EQ(true, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0).set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_2,
                                                                      true);
  EXPECT_EQ(true, dpll_control1.pll2_uses_hdmi_configuration_mode());
  EXPECT_EQ(true, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0).set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_3,
                                                                      true);
  EXPECT_EQ(true, dpll_control1.pll3_uses_hdmi_configuration_mode());
  EXPECT_EQ(true, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_3));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_1, false);
  EXPECT_EQ(false, dpll_control1.pll1_uses_hdmi_configuration_mode());
  EXPECT_EQ(false, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_2, false);
  EXPECT_EQ(false, dpll_control1.pll2_uses_hdmi_configuration_mode());
  EXPECT_EQ(false, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_3, false);
  EXPECT_EQ(false, dpll_control1.pll3_uses_hdmi_configuration_mode());
  EXPECT_EQ(false, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_3));
}

TEST(DisplayPllControl1Test, PllUsesHdmiConfigurationModeForDpll0) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_0, false);
  EXPECT_EQ(0xffff'ffff, dpll_control1.reg_value());
  EXPECT_EQ(false, dpll_control1.pll_uses_hdmi_configuration_mode(tgl_registers::Dpll::DPLL_0));
}

TEST(DisplayPllControl1Test, PllSpreadSpectrumClockingEnabled) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  dpll_control1.set_reg_value(0).set_pll_spread_spectrum_clocking_enabled(
      tgl_registers::Dpll::DPLL_1, true);
  EXPECT_EQ(true, dpll_control1.pll1_spread_spectrum_clocking_enabled());
  EXPECT_EQ(true, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0).set_pll_spread_spectrum_clocking_enabled(
      tgl_registers::Dpll::DPLL_2, true);
  EXPECT_EQ(true, dpll_control1.pll2_spread_spectrum_clocking_enabled());
  EXPECT_EQ(true, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0).set_pll_spread_spectrum_clocking_enabled(
      tgl_registers::Dpll::DPLL_3, true);
  EXPECT_EQ(true, dpll_control1.pll3_spread_spectrum_clocking_enabled());
  EXPECT_EQ(true, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_3));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_1, false);
  EXPECT_EQ(false, dpll_control1.pll1_spread_spectrum_clocking_enabled());
  EXPECT_EQ(false, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_2, false);
  EXPECT_EQ(false, dpll_control1.pll2_spread_spectrum_clocking_enabled());
  EXPECT_EQ(false, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_3, false);
  EXPECT_EQ(false, dpll_control1.pll3_spread_spectrum_clocking_enabled());
  EXPECT_EQ(false, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_3));
}

TEST(DisplayPllControl1Test, PllSpreadSpectrumClockingEnabledForDpll0) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_0, false);
  EXPECT_EQ(0xffff'ffff, dpll_control1.reg_value());
  EXPECT_EQ(false, dpll_control1.pll_spread_spectrum_clocking_enabled(tgl_registers::Dpll::DPLL_0));
}

TEST(DisplayPllControl1Test, PllDisplayPortDdiFrequencyMhzFieldMapping) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  // The test uses k2160Mhz because the bit pattern (0b101) requires 0->1
  // transitions on both edges of the bit field.

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        2'160);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k2160Mhz,
            dpll_control1.pll0_display_port_ddi_frequency_select());
  EXPECT_EQ(2'160, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_1,
                                                                        2'160);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k2160Mhz,
            dpll_control1.pll1_display_port_ddi_frequency_select());
  EXPECT_EQ(2'160, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_2,
                                                                        2'160);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k2160Mhz,
            dpll_control1.pll2_display_port_ddi_frequency_select());
  EXPECT_EQ(2'160, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_3,
                                                                        2'160);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k2160Mhz,
            dpll_control1.pll3_display_port_ddi_frequency_select());
  EXPECT_EQ(2'160, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_3));

  // The test uses k810Mhz because the bit pattern (0b010) requires 1->0
  // transitions on both edges of the bit field.

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0, 810);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k810Mhz,
            dpll_control1.pll0_display_port_ddi_frequency_select());
  EXPECT_EQ(810, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_1, 810);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k810Mhz,
            dpll_control1.pll1_display_port_ddi_frequency_select());
  EXPECT_EQ(810, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_2, 810);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k810Mhz,
            dpll_control1.pll2_display_port_ddi_frequency_select());
  EXPECT_EQ(810, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_3, 810);
  EXPECT_EQ(tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect::k810Mhz,
            dpll_control1.pll3_display_port_ddi_frequency_select());
  EXPECT_EQ(810, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_3));
}

TEST(DisplayPllControl1Test, PllDisplayPortDdiFrequencyMhzValueMapping) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  // The cases come from the reference manuals.
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 528-529
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 526-527

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        2'700);
  EXPECT_EQ(0b000, static_cast<int>(dpll_control1.pll0_display_port_ddi_frequency_select()));
  EXPECT_EQ(2'700, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        1'350);
  EXPECT_EQ(0b001, static_cast<int>(dpll_control1.pll0_display_port_ddi_frequency_select()));
  EXPECT_EQ(1'350, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        810);
  EXPECT_EQ(0b010, static_cast<int>(dpll_control1.pll0_display_port_ddi_frequency_select()));
  EXPECT_EQ(810, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        1'620);
  EXPECT_EQ(0b011, static_cast<int>(dpll_control1.pll0_display_port_ddi_frequency_select()));
  EXPECT_EQ(1'620, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        1'080);
  EXPECT_EQ(0b100, static_cast<int>(dpll_control1.pll0_display_port_ddi_frequency_select()));
  EXPECT_EQ(1'080, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0,
                                                                        2'160);
  EXPECT_EQ(0b101, static_cast<int>(dpll_control1.pll0_display_port_ddi_frequency_select()));
  EXPECT_EQ(2'160, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));
}

TEST(DisplayPllControl1Test, PllDisplayPortDdiFrequencyMhzInvalid) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  dpll_control1.set_reg_value(0).set_pll0_display_port_ddi_frequency_select(
      static_cast<tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect>(0b110));
  EXPECT_EQ(0, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll0_display_port_ddi_frequency_select(
      static_cast<tgl_registers::DisplayPllControl1::DisplayPortDdiFrequencySelect>(0b111));
  EXPECT_EQ(0, dpll_control1.pll_display_port_ddi_frequency_mhz(tgl_registers::Dpll::DPLL_0));
}

TEST(DisplayPllControl1Test, PllProgrammingEnabled) {
  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().FromValue(0);

  dpll_control1.set_reg_value(0).set_pll_programming_enabled(tgl_registers::Dpll::DPLL_0, true);
  EXPECT_EQ(true, dpll_control1.pll0_programming_enabled());
  EXPECT_EQ(true, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0).set_pll_programming_enabled(tgl_registers::Dpll::DPLL_1, true);
  EXPECT_EQ(true, dpll_control1.pll1_programming_enabled());
  EXPECT_EQ(true, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0).set_pll_programming_enabled(tgl_registers::Dpll::DPLL_2, true);
  EXPECT_EQ(true, dpll_control1.pll2_programming_enabled());
  EXPECT_EQ(true, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0).set_pll_programming_enabled(tgl_registers::Dpll::DPLL_3, true);
  EXPECT_EQ(true, dpll_control1.pll3_programming_enabled());
  EXPECT_EQ(true, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_3));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_programming_enabled(tgl_registers::Dpll::DPLL_0, false);
  EXPECT_EQ(false, dpll_control1.pll0_programming_enabled());
  EXPECT_EQ(false, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_0));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_programming_enabled(tgl_registers::Dpll::DPLL_1, false);
  EXPECT_EQ(false, dpll_control1.pll1_programming_enabled());
  EXPECT_EQ(false, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_1));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_programming_enabled(tgl_registers::Dpll::DPLL_2, false);
  EXPECT_EQ(false, dpll_control1.pll2_programming_enabled());
  EXPECT_EQ(false, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_2));

  dpll_control1.set_reg_value(0xffff'ffff)
      .set_pll_programming_enabled(tgl_registers::Dpll::DPLL_3, false);
  EXPECT_EQ(false, dpll_control1.pll3_programming_enabled());
  EXPECT_EQ(false, dpll_control1.pll_programming_enabled(tgl_registers::Dpll::DPLL_3));
}

TEST(DisplayPllDdiMapKabyLakeTest, DdiClockDisabled) {
  auto dpll_ddi_map = tgl_registers::DisplayPllDdiMapKabyLake::Get().FromValue(0);

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_A, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_a_clock_disabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_B, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_b_clock_disabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_B));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_C, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_c_clock_disabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_C));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_D, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_d_clock_disabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_D));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_E, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_e_clock_disabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_E));

  dpll_ddi_map.set_reg_value(0xffff'ffff).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_A, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_a_clock_disabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0xffff'ffff).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_B, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_b_clock_disabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_B));

  dpll_ddi_map.set_reg_value(0xffff'ffff).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_C, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_c_clock_disabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_C));

  dpll_ddi_map.set_reg_value(0xffff'ffff).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_D, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_d_clock_disabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_D));

  dpll_ddi_map.set_reg_value(0xffff'ffff).set_ddi_clock_disabled(tgl_registers::Ddi::DDI_E, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_e_clock_disabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_disabled(tgl_registers::Ddi::DDI_E));
}

TEST(DisplayPllDdiMapKabyLakeTest, DdiClockDisplayPll) {
  auto dpll_ddi_map = tgl_registers::DisplayPllDdiMapKabyLake::Get().FromValue(0);

  // The test uses DPLL3 because the bit pattern (0b11) requires 0->1
  // transitions on both edges of the bit field.

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_A,
                                                          tgl_registers::Dpll::DPLL_3);
  EXPECT_EQ(3u, dpll_ddi_map.ddi_a_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_3,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_B,
                                                          tgl_registers::Dpll::DPLL_3);
  EXPECT_EQ(3u, dpll_ddi_map.ddi_b_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_3,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_B));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_C,
                                                          tgl_registers::Dpll::DPLL_3);
  EXPECT_EQ(3u, dpll_ddi_map.ddi_c_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_3,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_C));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_D,
                                                          tgl_registers::Dpll::DPLL_3);
  EXPECT_EQ(3u, dpll_ddi_map.ddi_d_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_3,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_D));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_E,
                                                          tgl_registers::Dpll::DPLL_3);
  EXPECT_EQ(3u, dpll_ddi_map.ddi_e_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_3,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_E));

  // The test uses DPLL0 because the bit pattern (0b00) requires 1->0
  // transitions on both edges of the bit field.

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_A, tgl_registers::Dpll::DPLL_0);
  EXPECT_EQ(0u, dpll_ddi_map.ddi_a_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_0,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_B, tgl_registers::Dpll::DPLL_0);
  EXPECT_EQ(0u, dpll_ddi_map.ddi_b_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_0,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_B));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_C, tgl_registers::Dpll::DPLL_0);
  EXPECT_EQ(0u, dpll_ddi_map.ddi_c_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_0,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_C));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_D, tgl_registers::Dpll::DPLL_0);
  EXPECT_EQ(0u, dpll_ddi_map.ddi_d_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_0,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_D));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_E, tgl_registers::Dpll::DPLL_0);
  EXPECT_EQ(0u, dpll_ddi_map.ddi_e_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_0,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_E));

  // The test covers the bit patterns for DPLL1-2 to catches any renumbering of
  // the DPLL constants.

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_A,
                                                          tgl_registers::Dpll::DPLL_1);
  EXPECT_EQ(1u, dpll_ddi_map.ddi_a_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_1,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_display_pll(tgl_registers::Ddi::DDI_A,
                                                          tgl_registers::Dpll::DPLL_2);
  EXPECT_EQ(2u, dpll_ddi_map.ddi_a_clock_display_pll_index());
  EXPECT_EQ(tgl_registers::Dpll::DPLL_2,
            dpll_ddi_map.ddi_clock_display_pll(tgl_registers::Ddi::DDI_A));
}

TEST(DisplayPllDdiMapKabyLakeTest, DdiClockProgrammingEnabled) {
  auto dpll_ddi_map = tgl_registers::DisplayPllDdiMapKabyLake::Get().FromValue(0);

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_A, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_a_clock_programming_enabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_B, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_b_clock_programming_enabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_B));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_C, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_c_clock_programming_enabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_C));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_D, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_d_clock_programming_enabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_D));

  dpll_ddi_map.set_reg_value(0).set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_E, true);
  EXPECT_EQ(true, dpll_ddi_map.ddi_e_clock_programming_enabled());
  EXPECT_EQ(true, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_E));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_A, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_a_clock_programming_enabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_A));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_B, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_b_clock_programming_enabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_B));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_C, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_c_clock_programming_enabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_C));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_D, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_d_clock_programming_enabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_D));

  dpll_ddi_map.set_reg_value(0xffff'ffff)
      .set_ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_E, false);
  EXPECT_EQ(false, dpll_ddi_map.ddi_e_clock_programming_enabled());
  EXPECT_EQ(false, dpll_ddi_map.ddi_clock_programming_enabled(tgl_registers::Ddi::DDI_E));
}

TEST(DisplayPllDcoFrequencyKabyLakeTest, DcoFrequencyMultiplier) {
  auto dpll1_cfgcr1 =
      tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_multiplier(1);
  EXPECT_EQ(0u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(1u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(1, dpll1_cfgcr1.dco_frequency_multiplier());

  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_multiplier(0x8000);
  EXPECT_EQ(1u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(0u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(0x8000, dpll1_cfgcr1.dco_frequency_multiplier());

  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", pages 136-137.
  // The DCO frequency is 9064.72 Mhz, so the DCO multiplier is
  // (9,064,720 kHz * 32,768 fraction precision) / (24,000 kHz) = 12,376,364.

  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_multiplier(12'376'364);
  EXPECT_EQ(377u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(22828u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(12'376'364, dpll1_cfgcr1.dco_frequency_multiplier());

  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of HDMI on DDIC using
  // 296.703 MHz symbol clock", pages 137-138.
  // The DCO frequency is 8901.09 Mhz, so the DCO multiplier is
  // (8,901,090 kHz * 32,768 fraction precision) / (24,000 kHz) = 12,152,954.

  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_multiplier(12'152'954);
  EXPECT_EQ(370u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(28794u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(12'152'954, dpll1_cfgcr1.dco_frequency_multiplier());

  // Frequency value where both fields start and end with 1s, to check for field
  // trimming / incorrect overflowing.
  static constexpr int32_t kMultiplierBits = 0b110010011'110011010110011;
  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_multiplier(kMultiplierBits);
  EXPECT_EQ(0b110010011u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(0b110011010110011u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(kMultiplierBits, dpll1_cfgcr1.dco_frequency_multiplier());
}

TEST(DisplayPllDcoFrequencyKabyLakeTest, DcoFrequencyKhz) {
  auto dpll1_cfgcr1 =
      tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of DVI on DDIB using
  // 113.309 MHz symbol "clock", pages 136-137.
  // The DCO frequency is 9064.72 Mhz, so the DCO multiplier is
  // (9,064,720 kHz * 32,768 fraction precision) / (24,000 kHz) = 12,376,364.

  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_khz(9'064'720);
  EXPECT_EQ(377u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(22828u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(9'064'720, dpll1_cfgcr1.dco_frequency_khz());

  // Values from IHD-OS-KBL-Vol 12-1.17 section "Example of HDMI on DDIC using
  // 296.703 MHz symbol clock", pages 137-138.
  // The DCO frequency is 8901.09 Mhz, so the DCO multiplier is
  // (8,901,090 kHz * 32,768 fraction precision) / (24,000 kHz) = 12,152,954.

  dpll1_cfgcr1.set_reg_value(0).set_dco_frequency_khz(8'901'090);
  EXPECT_EQ(370u, dpll1_cfgcr1.dco_frequency_multiplier_integer());
  EXPECT_EQ(28794u, dpll1_cfgcr1.dco_frequency_multiplier_fraction());
  EXPECT_EQ(8'901'090, dpll1_cfgcr1.dco_frequency_khz());
}

TEST(DisplayPllDcoFrequencyKabyLakeTest, GetForDpll) {
  // The register MMIO addresses come from the reference manuals.
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 525
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 523

  auto dpll1_cfgcr1 =
      tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);
  EXPECT_EQ(0x6c040u, dpll1_cfgcr1.reg_addr());

  auto dpll2_cfgcr1 =
      tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_2)
          .FromValue(0);
  EXPECT_EQ(0x6c048u, dpll2_cfgcr1.reg_addr());

  auto dpll3_cfgcr1 =
      tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_3)
          .FromValue(0);
  EXPECT_EQ(0x6c050u, dpll3_cfgcr1.reg_addr());
}

TEST(DisplayPllDcoDividersKabyLakeTest, QP1Divider) {
  auto dpll1_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  dpll1_cfgcr2.set_reg_value(0).set_q_p1_divider(7);
  EXPECT_EQ(7u, dpll1_cfgcr2.q_p1_divider_select());
  EXPECT_EQ(true, dpll1_cfgcr2.q_p1_divider_select_enabled());
  EXPECT_EQ(7u, dpll1_cfgcr2.q_p1_divider());

  dpll1_cfgcr2.set_reg_value(0).set_q_p1_divider(1);
  EXPECT_EQ(1u, dpll1_cfgcr2.q_p1_divider_select());
  EXPECT_EQ(false, dpll1_cfgcr2.q_p1_divider_select_enabled());
  EXPECT_EQ(1u, dpll1_cfgcr2.q_p1_divider());

  dpll1_cfgcr2.set_reg_value(0).set_q_p1_divider(255);
  EXPECT_EQ(255u, dpll1_cfgcr2.q_p1_divider_select());
  EXPECT_EQ(true, dpll1_cfgcr2.q_p1_divider_select_enabled());
  EXPECT_EQ(255u, dpll1_cfgcr2.q_p1_divider());
}

TEST(DisplayPllDcoDividersKabyLakeTest, KP2Divider) {
  // The cases come from the reference manuals.
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 527
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 525

  auto dpll1_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  dpll1_cfgcr2.set_reg_value(0).set_k_p2_divider(5);
  EXPECT_EQ(0b00u, static_cast<unsigned>(dpll1_cfgcr2.k_p2_divider_select()));
  EXPECT_EQ(5u, dpll1_cfgcr2.k_p2_divider());

  dpll1_cfgcr2.set_reg_value(0).set_k_p2_divider(2);
  EXPECT_EQ(0b01u, static_cast<unsigned>(dpll1_cfgcr2.k_p2_divider_select()));
  EXPECT_EQ(2u, dpll1_cfgcr2.k_p2_divider());

  dpll1_cfgcr2.set_reg_value(0).set_k_p2_divider(3);
  EXPECT_EQ(0b10u, static_cast<unsigned>(dpll1_cfgcr2.k_p2_divider_select()));
  EXPECT_EQ(3u, dpll1_cfgcr2.k_p2_divider());

  dpll1_cfgcr2.set_reg_value(0).set_k_p2_divider(1);
  EXPECT_EQ(0b11u, static_cast<unsigned>(dpll1_cfgcr2.k_p2_divider_select()));
  EXPECT_EQ(1u, dpll1_cfgcr2.k_p2_divider());
}

TEST(DisplayPllDcoDividersKabyLakeTest, PP0Divider) {
  // The cases come from the reference manuals.
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 527
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 525

  auto dpll1_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider(1);
  EXPECT_EQ(0b000u, static_cast<unsigned>(dpll1_cfgcr2.p_p0_divider_select()));
  EXPECT_EQ(1u, dpll1_cfgcr2.p_p0_divider());

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider(2);
  EXPECT_EQ(0b001u, static_cast<unsigned>(dpll1_cfgcr2.p_p0_divider_select()));
  EXPECT_EQ(2u, dpll1_cfgcr2.p_p0_divider());

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider(3);
  EXPECT_EQ(0b010u, static_cast<unsigned>(dpll1_cfgcr2.p_p0_divider_select()));
  EXPECT_EQ(3u, dpll1_cfgcr2.p_p0_divider());

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider(7);
  EXPECT_EQ(0b100u, static_cast<unsigned>(dpll1_cfgcr2.p_p0_divider_select()));
  EXPECT_EQ(7u, dpll1_cfgcr2.p_p0_divider());
}

TEST(DisplayPllDcoDividersKabyLakeTest, PDividerInvalid) {
  auto dpll1_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider_select(
      static_cast<tgl_registers::DisplayPllDcoDividersKabyLake::PP0DividerSelect>(0b011));
  EXPECT_EQ(0u, dpll1_cfgcr2.p_p0_divider());

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider_select(
      static_cast<tgl_registers::DisplayPllDcoDividersKabyLake::PP0DividerSelect>(0b101));
  EXPECT_EQ(0u, dpll1_cfgcr2.p_p0_divider());

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider_select(
      static_cast<tgl_registers::DisplayPllDcoDividersKabyLake::PP0DividerSelect>(0b110));
  EXPECT_EQ(0u, dpll1_cfgcr2.p_p0_divider());

  dpll1_cfgcr2.set_reg_value(0).set_p_p0_divider_select(
      static_cast<tgl_registers::DisplayPllDcoDividersKabyLake::PP0DividerSelect>(0b111));
  EXPECT_EQ(0u, dpll1_cfgcr2.p_p0_divider());
}

TEST(DisplayPllDcoDividersKabyLakeTest, CenterFrequencyMhz) {
  // The cases come from the reference manuals.
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 527
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 525

  auto dpll1_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);

  dpll1_cfgcr2.set_reg_value(0).set_center_frequency_mhz(9'600);
  EXPECT_EQ(0b00u, static_cast<unsigned>(dpll1_cfgcr2.center_frequency_select()));
  EXPECT_EQ(9'600, dpll1_cfgcr2.center_frequency_mhz());

  dpll1_cfgcr2.set_reg_value(0).set_center_frequency_mhz(9'000);
  EXPECT_EQ(0b01u, static_cast<unsigned>(dpll1_cfgcr2.center_frequency_select()));
  EXPECT_EQ(9'000, dpll1_cfgcr2.center_frequency_mhz());

  dpll1_cfgcr2.set_reg_value(0).set_center_frequency_mhz(8'400);
  EXPECT_EQ(0b11u, static_cast<unsigned>(dpll1_cfgcr2.center_frequency_select()));
  EXPECT_EQ(8'400, dpll1_cfgcr2.center_frequency_mhz());
}

TEST(DisplayPllDcoDividersKabyLakeTest, GetForDpll) {
  // The register MMIO addresses come from the reference manuals.
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 525
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 523

  auto dpll1_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_1)
          .FromValue(0);
  EXPECT_EQ(0x6c044u, dpll1_cfgcr2.reg_addr());

  auto dpll2_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_2)
          .FromValue(0);
  EXPECT_EQ(0x6c04cu, dpll2_cfgcr2.reg_addr());

  auto dpll3_cfgcr2 =
      tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(tgl_registers::Dpll::DPLL_3)
          .FromValue(0);
  EXPECT_EQ(0x6c054u, dpll3_cfgcr2.reg_addr());
}

TEST(PllEnableTest, GetForSkylakeDpll) {
  // The register MMIO addresses come from the reference manuals.

  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 1121, 1122
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 1110, 1111

  auto lcpll1_ctl =
      tgl_registers::PllEnable::GetForSkylakeDpll(tgl_registers::Dpll::DPLL_0).FromValue(0);
  EXPECT_EQ(0x46010u, lcpll1_ctl.reg_addr());

  auto lcpll2_ctl =
      tgl_registers::PllEnable::GetForSkylakeDpll(tgl_registers::Dpll::DPLL_1).FromValue(0);
  EXPECT_EQ(0x46014u, lcpll2_ctl.reg_addr());

  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 1349-1350
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 1321-1322

  auto wrpll1_ctl =
      tgl_registers::PllEnable::GetForSkylakeDpll(tgl_registers::Dpll::DPLL_2).FromValue(0);
  EXPECT_EQ(0x46040u, wrpll1_ctl.reg_addr());

  auto wrpll2_ctl =
      tgl_registers::PllEnable::GetForSkylakeDpll(tgl_registers::Dpll::DPLL_3).FromValue(0);
  EXPECT_EQ(0x46060u, wrpll2_ctl.reg_addr());
}

TEST(PllEnableTest, GetForTigerLakeDpll) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 pages 655-656

  auto dpll0_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_0).FromValue(0);
  EXPECT_EQ(0x46010u, dpll0_enable.reg_addr());

  auto dpll1_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_1).FromValue(0);
  EXPECT_EQ(0x46014u, dpll1_enable.reg_addr());

  // TODO(fxbug.dev/110351): Add a test for DPLL 4, when we support it. The MMIO
  // address is 0x46018.

  auto tbt_pll_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_2).FromValue(0);
  EXPECT_EQ(0x46020u, tbt_pll_enable.reg_addr());

  auto mgpll1_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_TC_1).FromValue(0);
  EXPECT_EQ(0x46030u, mgpll1_enable.reg_addr());

  auto mgpll2_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_TC_2).FromValue(0);
  EXPECT_EQ(0x46034u, mgpll2_enable.reg_addr());

  auto mgpll3_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_TC_3).FromValue(0);
  EXPECT_EQ(0x46038u, mgpll3_enable.reg_addr());

  auto mgpll4_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_TC_4).FromValue(0);
  EXPECT_EQ(0x4603cu, mgpll4_enable.reg_addr());

  auto mgpll5_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_TC_5).FromValue(0);
  EXPECT_EQ(0x46040u, mgpll5_enable.reg_addr());

  auto mgpll6_enable =
      tgl_registers::PllEnable::GetForTigerLakeDpll(tgl_registers::Dpll::DPLL_TC_6).FromValue(0);
  EXPECT_EQ(0x46044u, mgpll6_enable.reg_addr());
}

TEST(DisplayPllStatusTest, PllLocked) {
  auto dpll_status = tgl_registers::DisplayPllStatus::Get().FromValue(0);

  dpll_status.set_reg_value(0).set_pll0_locked(true);
  EXPECT_EQ(true, dpll_status.pll_locked(tgl_registers::Dpll::DPLL_0));

  dpll_status.set_reg_value(0).set_pll1_locked(true);
  EXPECT_EQ(true, dpll_status.pll_locked(tgl_registers::Dpll::DPLL_1));

  dpll_status.set_reg_value(0).set_pll2_locked(true);
  EXPECT_EQ(true, dpll_status.pll_locked(tgl_registers::Dpll::DPLL_2));

  dpll_status.set_reg_value(0).set_pll3_locked(true);
  EXPECT_EQ(true, dpll_status.pll_locked(tgl_registers::Dpll::DPLL_3));
}

TEST(DisplayPllStatusTest, PllSemDone) {
  auto dpll_status = tgl_registers::DisplayPllStatus::Get().FromValue(0);

  dpll_status.set_reg_value(0).set_pll0_sem_done(true);
  EXPECT_EQ(true, dpll_status.pll_sem_done(tgl_registers::Dpll::DPLL_0));

  dpll_status.set_reg_value(0).set_pll1_sem_done(true);
  EXPECT_EQ(true, dpll_status.pll_sem_done(tgl_registers::Dpll::DPLL_1));

  dpll_status.set_reg_value(0).set_pll2_sem_done(true);
  EXPECT_EQ(true, dpll_status.pll_sem_done(tgl_registers::Dpll::DPLL_2));

  dpll_status.set_reg_value(0).set_pll3_sem_done(true);
  EXPECT_EQ(true, dpll_status.pll_sem_done(tgl_registers::Dpll::DPLL_3));
}

}  // namespace

}  // namespace i915_tgl
