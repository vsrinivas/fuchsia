// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-clk.h"

#include <zxtest/zxtest.h>

namespace {

constexpr uint64_t GetPllOutputFreq(uint64_t divfi, uint64_t divff) {
  return ((divff | ((divfi + 1) << 24)) * 5'000'000) >> 23;
}

}  // namespace

namespace clk {

class Vs680ClkTest : public zxtest::Test {
 public:
  Vs680ClkTest()
      : dut_(nullptr,
             ddk::MmioBuffer({chip_ctrl_regs_, 0, sizeof(chip_ctrl_regs_), ZX_HANDLE_INVALID}),
             ddk::MmioBuffer({cpu_pll_regs_, 0, sizeof(cpu_pll_regs_), ZX_HANDLE_INVALID}),
             ddk::MmioBuffer({avio_regs_, 0, sizeof(avio_regs_), ZX_HANDLE_INVALID}), 0) {}

  void SetUp() {
    memset(chip_ctrl_regs_, 0, sizeof(chip_ctrl_regs_));
    memset(cpu_pll_regs_, 0, sizeof(cpu_pll_regs_));
    memset(avio_regs_, 0, sizeof(avio_regs_));
  }

 protected:
  uint32_t chip_ctrl_regs_[0x800 / 4];
  uint32_t cpu_pll_regs_[0x20 / 4];
  uint32_t avio_regs_[0x200 / 4];

  Vs680Clk dut_;
};

TEST_F(Vs680ClkTest, SetRate) {
  cpu_pll_regs_[7] = 1;  // Set lock bit to skip sleep/log message.

  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 10'000'000));

  chip_ctrl_regs_[0x1c4] = 0b10000;  // Check that the bypass bit is cleared.
  cpu_pll_regs_[0] = 0b111011;       // Check that range, reset, and bypass are all cleared.
  cpu_pll_regs_[5] = 0x1f;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 20'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1c4], 0);
  EXPECT_EQ(cpu_pll_regs_[0], 0);
  EXPECT_EQ(cpu_pll_regs_[2], 4);  // divr
  EXPECT_EQ(cpu_pll_regs_[3], 1);  // divfi
  EXPECT_EQ(cpu_pll_regs_[4], 0);  // divff
  EXPECT_EQ(cpu_pll_regs_[5], 0);  // divq
  static_assert(GetPllOutputFreq(1, 0) == 20'000'000);

  chip_ctrl_regs_[0x1c4] = 0b10000;
  cpu_pll_regs_[0] = 0b111011;
  cpu_pll_regs_[5] = 0x1f;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 100'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1c4], 0);
  EXPECT_EQ(cpu_pll_regs_[0], 0);
  EXPECT_EQ(cpu_pll_regs_[2], 4);
  EXPECT_EQ(cpu_pll_regs_[3], 9);
  EXPECT_EQ(cpu_pll_regs_[4], 0);
  EXPECT_EQ(cpu_pll_regs_[5], 0);
  static_assert(GetPllOutputFreq(9, 0) == 100'000'000);

  chip_ctrl_regs_[0x1c4] = 0b10000;
  cpu_pll_regs_[0] = 0b111011;
  cpu_pll_regs_[5] = 0x1f;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 800'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1c4], 0);
  EXPECT_EQ(cpu_pll_regs_[0], 0);
  EXPECT_EQ(cpu_pll_regs_[2], 4);
  EXPECT_EQ(cpu_pll_regs_[3], 79);
  EXPECT_EQ(cpu_pll_regs_[4], 0);
  EXPECT_EQ(cpu_pll_regs_[5], 0);
  static_assert(GetPllOutputFreq(79, 0) == 800'000'000);

  chip_ctrl_regs_[0x1c4] = 0b10000;
  cpu_pll_regs_[0] = 0b111011;
  cpu_pll_regs_[5] = 0x1f;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 1'500'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1c4], 0);
  EXPECT_EQ(cpu_pll_regs_[0], 0);
  EXPECT_EQ(cpu_pll_regs_[2], 4);
  EXPECT_EQ(cpu_pll_regs_[3], 149);
  EXPECT_EQ(cpu_pll_regs_[4], 0);
  EXPECT_EQ(cpu_pll_regs_[5], 0);
  static_assert(GetPllOutputFreq(149, 0) == 1'500'000'000);

  chip_ctrl_regs_[0x1c4] = 0b10000;
  cpu_pll_regs_[0] = 0b111011;
  cpu_pll_regs_[5] = 0x1f;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 2'200'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1c4], 0);
  EXPECT_EQ(cpu_pll_regs_[0], 0);
  EXPECT_EQ(cpu_pll_regs_[2], 4);
  EXPECT_EQ(cpu_pll_regs_[3], 219);
  EXPECT_EQ(cpu_pll_regs_[4], 0);
  EXPECT_EQ(cpu_pll_regs_[5], 0);
  static_assert(GetPllOutputFreq(219, 0) == 2'200'000'000);

  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kCpuPll, 2'500'000'000));

  avio_regs_[0x4c] = 0b100;
  avio_regs_[0x0a + 0] = 0b111011;
  avio_regs_[0x0a + 5] = 0x1f;
  avio_regs_[0x0a + 7] = 1;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kAPll0, 196'608'000));
  EXPECT_EQ(avio_regs_[0x4c], 0);
  EXPECT_EQ(avio_regs_[0x0a + 0], 0);
  EXPECT_EQ(avio_regs_[0x0a + 2], 4);
  EXPECT_EQ(avio_regs_[0x0a + 3], 18);
  EXPECT_EQ(avio_regs_[0x0a + 4], 11086384);
  EXPECT_EQ(avio_regs_[0x0a + 5], 0);
  static_assert(GetPllOutputFreq(18, 11086384) == 196'607'999);

  avio_regs_[0x4c] = 0b10;
  avio_regs_[0x1c + 0] = 0b111011;
  avio_regs_[0x1c + 5] = 0x1f;
  avio_regs_[0x1c + 7] = 1;
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kVPll1, 180'633'600));
  EXPECT_EQ(avio_regs_[0x4c], 0);
  EXPECT_EQ(avio_regs_[0x1c + 0], 0);
  EXPECT_EQ(avio_regs_[0x1c + 2], 4);
  EXPECT_EQ(avio_regs_[0x1c + 3], 17);
  EXPECT_EQ(avio_regs_[0x1c + 4], 1063004);
  EXPECT_EQ(avio_regs_[0x1c + 5], 0);
  static_assert(GetPllOutputFreq(17, 1063004) == 180'633'599);

  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kAPll1, 1'300'000'000));
}

TEST_F(Vs680ClkTest, QuerySupportedRate) {
  uint64_t hz;

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSysPll0, 20'000'000, &hz));
  EXPECT_EQ(hz, 20'000'000);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSysPll0, 1'000'000'000, &hz));
  EXPECT_EQ(hz, 1'000'000'000);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSysPll0, 1'200'000'000, &hz));
  EXPECT_EQ(hz, 1'200'000'000);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSysPll0, 1'300'000'000, &hz));
  EXPECT_EQ(hz, 1'200'000'000);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kVPll0, 196'608'000, &hz));
  EXPECT_EQ(hz, 196'607'999);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kAPll1, 180'633'600, &hz));
  EXPECT_EQ(hz, 180'633'599);
}

TEST_F(Vs680ClkTest, GetRate) {
  uint64_t hz;

  chip_ctrl_regs_[0x1c4] = 0;
  chip_ctrl_regs_[0x80 + 0] = 0;
  chip_ctrl_regs_[0x80 + 2] = 4;
  chip_ctrl_regs_[0x80 + 3] = 219;
  chip_ctrl_regs_[0x80 + 4] = 0;
  chip_ctrl_regs_[0x80 + 5] = 0;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 2'200'000'000);

  chip_ctrl_regs_[0x1c4] = 1;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 25'000'000);

  chip_ctrl_regs_[0x1c4] = 0;
  chip_ctrl_regs_[0x80 + 0] = 0b10;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 25'000'000);

  chip_ctrl_regs_[0x80 + 0] = 0;
  chip_ctrl_regs_[0x80 + 5] = 3;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 550'000'000);

  chip_ctrl_regs_[0x80 + 5] = 31;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 68'750'000);

  chip_ctrl_regs_[0x80 + 2] = 9;
  chip_ctrl_regs_[0x80 + 5] = 0;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 1'100'000'000);

  chip_ctrl_regs_[0x80 + 2] = 24;
  chip_ctrl_regs_[0x80 + 3] = 39;
  chip_ctrl_regs_[0x80 + 5] = 7;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 10'000'000);

  chip_ctrl_regs_[0x80 + 2] = 4;
  chip_ctrl_regs_[0x80 + 3] = 18;
  chip_ctrl_regs_[0x80 + 4] = 11086384;
  chip_ctrl_regs_[0x80 + 5] = 0;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 196'607'999);

  chip_ctrl_regs_[0x80 + 3] = 17;
  chip_ctrl_regs_[0x80 + 4] = 1063004;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSysPll0, &hz));
  EXPECT_EQ(hz, 180'633'599);
}

}  // namespace clk
