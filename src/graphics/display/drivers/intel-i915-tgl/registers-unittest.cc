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

}  // namespace

}  // namespace i915_tgl
