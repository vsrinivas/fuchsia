// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <fbl/algorithm.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace ddk_mock_test {

TEST(MockMmioReg, CopyFrom) {
  ddk_mock::MockMmioReg reg_array_1[0x100];
  ddk_mock::MockMmioReg reg_array_2[0x100];

  ddk_mock::MockMmioRegRegion reg_region_1(reg_array_1, sizeof(uint32_t),
                                           fbl::count_of(reg_array_1));
  ddk_mock::MockMmioRegRegion reg_region_2(reg_array_2, sizeof(uint32_t),
                                           fbl::count_of(reg_array_2));

  ddk::MmioBuffer dut_1 = reg_region_1.GetMmioBuffer();
  ddk::MmioBuffer dut_2 = reg_region_2.GetMmioBuffer();

  constexpr uint32_t reg_values[] = {0xdb5a95fd, 0xc1c8f880, 0x733c2bed, 0xf74e857c};
  for (size_t i = 0; i < fbl::count_of(reg_values); i++) {
    reg_region_1[0x10 + (i * 4)].ExpectRead(reg_values[i]);
    reg_region_2[0x40 + (i * 4)].ExpectWrite(reg_values[i]);
  }

  dut_2.CopyFrom32(dut_1, 0x10, 0x40, 4);

  ASSERT_NO_FATAL_FAILURES(reg_region_1.VerifyAll());
  ASSERT_NO_FATAL_FAILURES(reg_region_2.VerifyAll());
}

TEST(MockMmioReg, View) {
  ddk_mock::MockMmioReg reg_array[0x100];

  ddk_mock::MockMmioRegRegion reg_region(reg_array, sizeof(uint32_t), fbl::count_of(reg_array));

  ddk::MmioBuffer dut = reg_region.GetMmioBuffer();
  ddk::MmioView dut_view_1 = dut.View(0x40);
  ddk::MmioView dut_view_2 = dut_view_1.View(0x20);

  reg_region[0x20].ExpectRead(0x8ed43ca9).ExpectWrite(0x7a5da8d8);
  reg_region[0x80].ExpectRead(0x5be3254c).ExpectWrite(0x6ba7d0af);
  reg_region[0x60].ExpectRead(0xa1026dfe).ExpectWrite(0x0164bff2);

  EXPECT_EQ(dut.Read32(0x20), 0x8ed43ca9);
  EXPECT_EQ(dut_view_1.Read32(0x40), 0x5be3254c);
  EXPECT_EQ(dut_view_2.Read32(0), 0xa1026dfe);

  dut.Write32(0x7a5da8d8, 0x20);
  dut_view_1.Write32(0x6ba7d0af, 0x40);
  dut_view_2.Write32(0x0164bff2, 0);

  ASSERT_NO_FATAL_FAILURES(reg_region.VerifyAll());
}

}  // namespace ddk_mock_test
