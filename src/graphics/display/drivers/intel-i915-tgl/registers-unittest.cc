// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

#include <gtest/gtest.h>

namespace i915_tgl {

namespace {

TEST(RegistersTest, CdClockCtlFreqDecimal) {
  // Test cases from IHD-OS-KBL-Vol 2c-1.17 Part 1 page 329.
  // Same cases are in IHD-OS-SKL-Vol 2c-05.16 Part 1 page 326.
  EXPECT_EQ(0b01'0011'0011'1u, tgl_registers::CdClockCtl::FreqDecimal(308'570));
  EXPECT_EQ(0b01'0101'0000'1u, tgl_registers::CdClockCtl::FreqDecimal(337'500));
  EXPECT_EQ(0b01'1010'1111'0u, tgl_registers::CdClockCtl::FreqDecimal(432'000));
  EXPECT_EQ(0b01'1100'0001'0u, tgl_registers::CdClockCtl::FreqDecimal(450'000));
  EXPECT_EQ(0b10'0001'1011'0u, tgl_registers::CdClockCtl::FreqDecimal(540'000));
  EXPECT_EQ(0b10'0110'1000'0u, tgl_registers::CdClockCtl::FreqDecimal(617'140));
  EXPECT_EQ(0b10'1010'0010'0u, tgl_registers::CdClockCtl::FreqDecimal(675'000));

  // Test cases from IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 221-222.
  // Same cases are in IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 181-182.
  EXPECT_EQ(0b00'1010'0111'0u, tgl_registers::CdClockCtl::FreqDecimal(168'000));
  EXPECT_EQ(0b00'1010'1100'0u, tgl_registers::CdClockCtl::FreqDecimal(172'800));
  EXPECT_EQ(0b00'1011'0010'0u, tgl_registers::CdClockCtl::FreqDecimal(179'200));
  EXPECT_EQ(0b00'1011'0011'0u, tgl_registers::CdClockCtl::FreqDecimal(180'000));
  EXPECT_EQ(0b00'1011'1111'0u, tgl_registers::CdClockCtl::FreqDecimal(192'000));
  EXPECT_EQ(0b01'0011'0010'0u, tgl_registers::CdClockCtl::FreqDecimal(307'200));
  EXPECT_EQ(0b01'0011'0111'0u, tgl_registers::CdClockCtl::FreqDecimal(312'000));
  EXPECT_EQ(0b01'0100'0011'0u, tgl_registers::CdClockCtl::FreqDecimal(324'000));
  EXPECT_EQ(0b01'0100'0101'1u, tgl_registers::CdClockCtl::FreqDecimal(326'400));
  EXPECT_EQ(0b01'1101'1111'0u, tgl_registers::CdClockCtl::FreqDecimal(480'000));
  EXPECT_EQ(0b10'0010'0111'0u, tgl_registers::CdClockCtl::FreqDecimal(552'000));
  EXPECT_EQ(0b10'0010'1100'0u, tgl_registers::CdClockCtl::FreqDecimal(556'800));
  EXPECT_EQ(0b10'1000'0111'0u, tgl_registers::CdClockCtl::FreqDecimal(648'000));
  EXPECT_EQ(0b10'1000'1100'0u, tgl_registers::CdClockCtl::FreqDecimal(652'800));
}

TEST(RegistersTest, PowerWellControlAux_TigerLake) {
  auto reg = tgl_registers::PowerWellControlAux::Get().FromValue(0);

  // AUX IO Power request
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_A, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_a(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_A), 0u);
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_B, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_b(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_B), 0u);
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_C, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_c(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_C), 0u);

  // USB-C Power request
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_TC_1,
                                                           /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_usb_c_1(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_1), 0u);

  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_TC_2,
                                                           /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_usb_c_2(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_2), 0u);
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_TC_3,
                                                           /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_usb_c_3(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_3), 0u);
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_TC_4,
                                                           /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_usb_c_4(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_4), 0u);
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_TC_5,
                                                           /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_usb_c_5(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_5), 0u);
  reg.set_reg_value(0).set_power_on_request_combo_or_usb_c(tgl_registers::DDI_TC_6,
                                                           /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_usb_c_6(), 1u);
  EXPECT_EQ(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_6), 0u);

  // Thunderbolt Power request
  reg.set_reg_value(0).set_power_on_request_thunderbolt(tgl_registers::DDI_TC_1, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_thunderbolt_1(), 1u);
  EXPECT_EQ(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_1), 0u);
  reg.set_reg_value(0).set_power_on_request_thunderbolt(tgl_registers::DDI_TC_2, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_thunderbolt_2(), 1u);
  EXPECT_EQ(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_2), 0u);
  reg.set_reg_value(0).set_power_on_request_thunderbolt(tgl_registers::DDI_TC_3, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_thunderbolt_3(), 1u);
  EXPECT_EQ(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_3), 0u);
  reg.set_reg_value(0).set_power_on_request_thunderbolt(tgl_registers::DDI_TC_4, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_thunderbolt_4(), 1u);
  EXPECT_EQ(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_4), 0u);
  reg.set_reg_value(0).set_power_on_request_thunderbolt(tgl_registers::DDI_TC_5, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_thunderbolt_5(), 1u);
  EXPECT_EQ(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_5), 0u);
  reg.set_reg_value(0).set_power_on_request_thunderbolt(tgl_registers::DDI_TC_6, /*enabled=*/true);
  EXPECT_EQ(reg.power_on_request_thunderbolt_6(), 1u);
  EXPECT_EQ(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_6), 0u);

  // AUX IO Power state
  reg.set_reg_value(0).set_powered_on_a(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_A));
  reg.set_reg_value(0).set_powered_on_b(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_B));
  reg.set_reg_value(0).set_powered_on_c(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_C));

  // USB-C Power state
  reg.set_reg_value(0).set_powered_on_usb_c_1(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_1));
  reg.set_reg_value(0).set_powered_on_usb_c_2(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_2));
  reg.set_reg_value(0).set_powered_on_usb_c_3(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_3));
  reg.set_reg_value(0).set_powered_on_usb_c_4(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_4));
  reg.set_reg_value(0).set_powered_on_usb_c_5(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_5));
  reg.set_reg_value(0).set_powered_on_usb_c_6(1);
  EXPECT_TRUE(reg.powered_on_combo_or_usb_c(tgl_registers::DDI_TC_6));

  // Thunderbolt Power state
  reg.set_reg_value(0).set_powered_on_thunderbolt_1(1);
  EXPECT_TRUE(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_1));
  reg.set_reg_value(0).set_powered_on_thunderbolt_2(1);
  EXPECT_TRUE(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_2));
  reg.set_reg_value(0).set_powered_on_thunderbolt_3(1);
  EXPECT_TRUE(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_3));
  reg.set_reg_value(0).set_powered_on_thunderbolt_4(1);
  EXPECT_TRUE(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_4));
  reg.set_reg_value(0).set_powered_on_thunderbolt_5(1);
  EXPECT_TRUE(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_5));
  reg.set_reg_value(0).set_powered_on_thunderbolt_6(1);
  EXPECT_TRUE(reg.powered_on_thunderbolt(tgl_registers::DDI_TC_6));
}

TEST(DataBufferControlTest, GetForSlice) {
  // The register MMIO addresses come from the reference manual.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 331
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 309
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 430

  auto dbuf_ctl_s1 = tgl_registers::DataBufferControl::GetForSlice(0).FromValue(0);
  EXPECT_EQ(0x45008u, dbuf_ctl_s1.reg_addr());

  auto dbuf_ctl_s2 = tgl_registers::DataBufferControl::GetForSlice(1).FromValue(0);
  EXPECT_EQ(0x44fe8u, dbuf_ctl_s2.reg_addr());
}

TEST(DataBufferControl2Test, GetForSlice) {
  // The register MMIO addresses come from the reference manual.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 333
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 311

  auto dbuf_ctl2_s1 = tgl_registers::DataBufferControl2::GetForSlice(0).FromValue(0);
  EXPECT_EQ(0x44ffcu, dbuf_ctl2_s1.reg_addr());

  auto dbuf_ctl2_s2 = tgl_registers::DataBufferControl2::GetForSlice(1).FromValue(0);
  EXPECT_EQ(0x44fe4u, dbuf_ctl2_s2.reg_addr());
}

}  // namespace

}  // namespace i915_tgl
