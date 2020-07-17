// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-clk.h"

#include <mmio-ptr/fake.h>
#include <zxtest/zxtest.h>

#include "vs680-clk-reg.h"

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
             ddk::MmioBuffer(
                 {FakeMmioPtr(chip_ctrl_regs_), 0, sizeof(chip_ctrl_regs_), ZX_HANDLE_INVALID}),
             ddk::MmioBuffer(
                 {FakeMmioPtr(cpu_pll_regs_), 0, sizeof(cpu_pll_regs_), ZX_HANDLE_INVALID}),
             ddk::MmioBuffer({FakeMmioPtr(avio_regs_), 0, sizeof(avio_regs_), ZX_HANDLE_INVALID}),
             zx::sec(0)) {}

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

TEST_F(Vs680ClkTest, PllSetRate) {
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

TEST_F(Vs680ClkTest, PllQuerySupportedRate) {
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

TEST_F(Vs680ClkTest, PllGetRate) {
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

  avio_regs_[0x0a + 0] = 0;
  avio_regs_[0x0a + 2] = 24;
  avio_regs_[0x0a + 3] = 39;
  avio_regs_[0x0a + 5] = 7;
  avio_regs_[0x4c] = 0;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kAPll0, &hz));
  EXPECT_EQ(hz, 10'000'000);

  avio_regs_[0x0a + 0] = 0b10;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kAPll0, &hz));
  EXPECT_EQ(hz, 25'000'000);

  avio_regs_[0x0a + 0] = 0;
  avio_regs_[0x4c + 0] = 0b100;
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kAPll0, &hz));
  EXPECT_EQ(hz, 10'000'000);
}

TEST_F(Vs680ClkTest, PllEnableDisable) {
  chip_ctrl_regs_[0x4c] = 0;
  EXPECT_OK(dut_.ClockImplDisable(vs680::kAPll0));
  EXPECT_EQ(avio_regs_[0x4c], 0b100);

  bool enabled = true;
  EXPECT_OK(dut_.ClockImplIsEnabled(vs680::kAPll0, &enabled));
  EXPECT_FALSE(enabled);

  avio_regs_[0x4c] = 1;
  EXPECT_OK(dut_.ClockImplEnable(vs680::kVPll0));
  EXPECT_EQ(avio_regs_[0x4c], 0);

  EXPECT_OK(dut_.ClockImplIsEnabled(vs680::kVPll0, &enabled));
  EXPECT_TRUE(enabled);

  avio_regs_[0x4c] = 0;
  EXPECT_OK(dut_.ClockImplDisable(vs680::kAPll1));
  EXPECT_EQ(avio_regs_[0x4c], 0b1000);

  EXPECT_OK(dut_.ClockImplIsEnabled(vs680::kAPll1, &enabled));
  EXPECT_FALSE(enabled);

  avio_regs_[0x4c] = 0b10;
  EXPECT_OK(dut_.ClockImplEnable(vs680::kVPll1));
  EXPECT_EQ(avio_regs_[0x4c], 0);

  EXPECT_OK(dut_.ClockImplIsEnabled(vs680::kVPll1, &enabled));
  EXPECT_TRUE(enabled);

  EXPECT_NOT_OK(dut_.ClockImplEnable(vs680::kSysPll0));
  EXPECT_NOT_OK(dut_.ClockImplDisable(vs680::kSysPll1));
  EXPECT_NOT_OK(dut_.ClockImplIsEnabled(vs680::kCpuPll, &enabled));
}

TEST_F(Vs680ClkTest, ClockMuxSetRate) {
  chip_ctrl_regs_[0x1c4] = 0;

  // SYSPLL0: 100 MHz
  chip_ctrl_regs_[0x80 + 0] = 0;
  chip_ctrl_regs_[0x80 + 2] = 4;
  chip_ctrl_regs_[0x80 + 3] = 9;
  chip_ctrl_regs_[0x80 + 5] = 0;

  // SYSPLL1: 200 MHz
  chip_ctrl_regs_[0x88 + 0] = 0;
  chip_ctrl_regs_[0x88 + 2] = 4;
  chip_ctrl_regs_[0x88 + 3] = 19;
  chip_ctrl_regs_[0x88 + 5] = 0;

  // SYSPLL2: 1.2 GHz
  chip_ctrl_regs_[0x90 + 0] = 0;
  chip_ctrl_regs_[0x90 + 2] = 4;
  chip_ctrl_regs_[0x90 + 3] = 119;
  chip_ctrl_regs_[0x90 + 5] = 0;

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv2)
                               .set_clk_d3_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1)
                               .reg_value();
  // Divide SYSPLL1 by 8: clk_d3_switch cleared, clk_switch set, clk_sel changed to kDiv8.
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 25'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_sel(ClockMux::kDiv8)
                                        .set_clk_switch(1)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll1)
                                        .reg_value());

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv2)
                               .set_clk_d3_switch(1)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1)
                               .set_clk_en(1)
                               .reg_value();
  // Pass through SYSPLL1: clk_d3_switch and clk_switch cleared.
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 200'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_sel(ClockMux::kDiv2)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll1)
                                        .set_clk_en(1)
                                        .reg_value());

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv6)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .set_clk_en(1)
                               .reg_value();
  // Divide SYSPLL2 by 3: clk_d3_switch set.
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 400'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_sel(ClockMux::kDiv6)
                                        .set_clk_d3_switch(1)
                                        .set_clk_switch(1)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll2)
                                        .set_clk_en(1)
                                        .reg_value());

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv12)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // Divide SYSPLL0 by 4: clk_switch set, clk_sel changed to kDiv4.
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 25'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_sel(ClockMux::kDiv4)
                                        .set_clk_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll2)
                                        .reg_value());

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv6)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // Divide SYSPLL2 by 12: clk_switch set, clk_sel changed to kDiv12.
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 100'000'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_sel(ClockMux::kDiv12)
                                        .set_clk_switch(1)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll2)
                                        .reg_value());

  chip_ctrl_regs_[0x1c4] = 0b100;
  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv8)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // Divide bypassed SYSPLL2 by 4: clk_switch set, clk_sel changed to kDiv4.
  EXPECT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 6'250'000));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_sel(ClockMux::kDiv4)
                                        .set_clk_switch(1)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll2)
                                        .reg_value());

  chip_ctrl_regs_[0x1c4] = 0;

  // Divide by 24, 48 not supported.
  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 50'000'000));
  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 25'000'000));

  // Unsupported input selections.
  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll0F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 100'000'000));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 100'000'000));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 100'000'000));

  // Invalid input selection.
  chip_ctrl_regs_[0x1ed] =
      ClockMux::Get().FromValue(0).set_clk_pll_switch(1).set_clk_pll_sel(7).reg_value();
  EXPECT_NOT_OK(dut_.ClockImplSetRate(vs680::kSd0Clock, 100'000'000));
}

TEST_F(Vs680ClkTest, ClockMuxQuerySupportedRate) {
  chip_ctrl_regs_[0x1c4] = 0;

  // SYSPLL0: 100 MHz
  chip_ctrl_regs_[0x80 + 0] = 0;
  chip_ctrl_regs_[0x80 + 2] = 4;
  chip_ctrl_regs_[0x80 + 3] = 9;
  chip_ctrl_regs_[0x80 + 5] = 0;

  // SYSPLL1: 200 MHz
  chip_ctrl_regs_[0x88 + 0] = 0;
  chip_ctrl_regs_[0x88 + 2] = 4;
  chip_ctrl_regs_[0x88 + 3] = 19;
  chip_ctrl_regs_[0x88 + 5] = 0;

  // SYSPLL2: 1.2 GHz
  chip_ctrl_regs_[0x90 + 0] = 0;
  chip_ctrl_regs_[0x90 + 2] = 4;
  chip_ctrl_regs_[0x90 + 3] = 119;
  chip_ctrl_regs_[0x90 + 5] = 0;

  uint64_t hz;

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1)
                               .reg_value();
  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 400'000'000, &hz));
  EXPECT_EQ(hz, 200'000'000);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 25'000'000, &hz));
  EXPECT_EQ(hz, 25'000'000);

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 150'000'000, &hz));
  EXPECT_EQ(hz, 100'000'000);

  EXPECT_NOT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 3'125'000, &hz));

  chip_ctrl_regs_[0x1c4] = 0b10;

  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 3'125'000, &hz));
  EXPECT_EQ(hz, 3'125'000);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get().FromValue(0).reg_value();
  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 400'000'000, &hz));
  EXPECT_EQ(hz, 100'000'000);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  EXPECT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 400'000'000, &hz));
  EXPECT_EQ(hz, 400'000'000);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll0F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 100'000'000, &hz));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 100'000'000, &hz));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 100'000'000, &hz));

  chip_ctrl_regs_[0x1ed] =
      ClockMux::Get().FromValue(0).set_clk_pll_switch(1).set_clk_pll_sel(7).reg_value();
  EXPECT_NOT_OK(dut_.ClockImplQuerySupportedRate(vs680::kSd0Clock, 100'000'000, &hz));
}

TEST_F(Vs680ClkTest, ClockMuxGetRate) {
  chip_ctrl_regs_[0x1c4] = 0;

  // SYSPLL0: 100 MHz
  chip_ctrl_regs_[0x80 + 0] = 0;
  chip_ctrl_regs_[0x80 + 2] = 4;
  chip_ctrl_regs_[0x80 + 3] = 9;
  chip_ctrl_regs_[0x80 + 5] = 0;

  // SYSPLL1: 200 MHz
  chip_ctrl_regs_[0x88 + 0] = 0;
  chip_ctrl_regs_[0x88 + 2] = 4;
  chip_ctrl_regs_[0x88 + 3] = 19;
  chip_ctrl_regs_[0x88 + 5] = 0;

  // SYSPLL2: 1.2 GHz
  chip_ctrl_regs_[0x90 + 0] = 0;
  chip_ctrl_regs_[0x90 + 2] = 4;
  chip_ctrl_regs_[0x90 + 3] = 119;
  chip_ctrl_regs_[0x90 + 5] = 0;

  uint64_t hz;

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv6)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // SYSPLL2 not divided.
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));
  EXPECT_EQ(hz, 1'200'000'000);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv6)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // SYSPLL2 divided by 6.
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));
  EXPECT_EQ(hz, 200'000'000);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv6)
                               .set_clk_d3_switch(1)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // SYSPLL2 divided by 3.
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));
  EXPECT_EQ(hz, 400'000'000);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv4)
                               .set_clk_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // SYSPLL0 divided by 4.
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));
  EXPECT_EQ(hz, 25'000'000);

  chip_ctrl_regs_[0x1c4] = 1;
  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv8)
                               .set_clk_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  // SYSPLL0 bypassed and divided by 8.
  EXPECT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));
  EXPECT_EQ(hz, 3'125'000);

  // Unsupported input selections.
  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll0F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2F)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));

  // Invalid input selection.
  chip_ctrl_regs_[0x1ed] =
      ClockMux::Get().FromValue(0).set_clk_pll_switch(1).set_clk_pll_sel(7).reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));

  // Unsupported divider selections.
  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv24)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(ClockMux::kDiv48)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));

  // Invalid divider selection.
  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_sel(0)
                               .set_clk_switch(1)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2)
                               .reg_value();
  EXPECT_NOT_OK(dut_.ClockImplGetRate(vs680::kSd0Clock, &hz));
}

TEST_F(Vs680ClkTest, ClockMuxEnableDisable) {
  chip_ctrl_regs_[0x1ed] = 0;

  bool enabled;
  EXPECT_OK(dut_.ClockImplIsEnabled(vs680::kSd0Clock, &enabled));
  EXPECT_FALSE(enabled);

  EXPECT_OK(dut_.ClockImplEnable(vs680::kSd0Clock));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], 1);

  EXPECT_OK(dut_.ClockImplIsEnabled(vs680::kSd0Clock, &enabled));
  EXPECT_TRUE(enabled);

  EXPECT_OK(dut_.ClockImplDisable(vs680::kSd0Clock));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], 0);
}

TEST_F(Vs680ClkTest, ClockMuxInput) {
  chip_ctrl_regs_[0x1ed] = 0;

  uint32_t num_inputs;
  EXPECT_OK(dut_.ClockImplGetNumInputs(vs680::kSd0Clock, &num_inputs));
  EXPECT_EQ(num_inputs, 6);

  EXPECT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, vs680::kClockInputSysPll1));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll1)
                                        .reg_value());

  uint32_t index;
  EXPECT_OK(dut_.ClockImplGetInput(vs680::kSd0Clock, &index));
  EXPECT_EQ(index, vs680::kClockInputSysPll1);

  EXPECT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, vs680::kClockInputSysPll0));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed],
            ClockMux::Get().FromValue(0).set_clk_pll_sel(vs680::kClockInputSysPll1).reg_value());

  EXPECT_OK(dut_.ClockImplGetInput(vs680::kSd0Clock, &index));
  EXPECT_EQ(index, vs680::kClockInputSysPll0);

  EXPECT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, vs680::kClockInputSysPll2));
  EXPECT_EQ(chip_ctrl_regs_[0x1ed], ClockMux::Get()
                                        .FromValue(0)
                                        .set_clk_pll_switch(1)
                                        .set_clk_pll_sel(vs680::kClockInputSysPll2)
                                        .reg_value());

  EXPECT_OK(dut_.ClockImplGetInput(vs680::kSd0Clock, &index));
  EXPECT_EQ(index, vs680::kClockInputSysPll2);

  EXPECT_NOT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, vs680::kClockInputSysPll0F));
  EXPECT_NOT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, vs680::kClockInputSysPll1F));
  EXPECT_NOT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, vs680::kClockInputSysPll2F));
  EXPECT_NOT_OK(dut_.ClockImplSetInput(vs680::kSd0Clock, 7));

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll0F)
                               .reg_value();
  EXPECT_OK(dut_.ClockImplGetInput(vs680::kSd0Clock, &index));
  EXPECT_EQ(index, vs680::kClockInputSysPll0F);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll1F)
                               .reg_value();
  EXPECT_OK(dut_.ClockImplGetInput(vs680::kSd0Clock, &index));
  EXPECT_EQ(index, vs680::kClockInputSysPll1F);

  chip_ctrl_regs_[0x1ed] = ClockMux::Get()
                               .FromValue(0)
                               .set_clk_pll_switch(1)
                               .set_clk_pll_sel(vs680::kClockInputSysPll2F)
                               .reg_value();
  EXPECT_OK(dut_.ClockImplGetInput(vs680::kSd0Clock, &index));
  EXPECT_EQ(index, vs680::kClockInputSysPll2F);
}

}  // namespace clk
