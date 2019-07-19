// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-clk.h"

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/msm8x53/msm8x53-clock.h>

#include "msm8x53-clk-regs.h"

namespace clk {

class Msm8x53ClkTest : public Msm8x53Clk {
 public:
  Msm8x53ClkTest(ddk_mock::MockMmioRegRegion& mmio)
      : Msm8x53Clk(nullptr, ddk::MmioBuffer(mmio.GetMmioBuffer())) {}
};

// Test MND divider path.
TEST(ClkTest8x53, TestSetRcgMnd) {
  auto cc_regs_arr = std::make_unique<ddk_mock::MockMmioReg[]>(msm8x53::kCcSize);
  ddk_mock::MockMmioRegRegion cc_regs(cc_regs_arr.get(), sizeof(uint32_t), msm8x53::kCcSize);
  Msm8x53ClkTest clk(cc_regs);

  const MsmClkRcg& kTestClock = kMsmClkRcgs[msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart2AppsClkSrc)];
  const RcgFrequencyTable kTestFrequency = kTestClock.Table()[0];
  cc_regs[kTestClock.MReg()].ExpectWrite(kTestFrequency.m());
  cc_regs[kTestClock.NReg()].ExpectWrite(kTestFrequency.n());
  cc_regs[kTestClock.DReg()].ExpectWrite(kTestFrequency.d());
  cc_regs[kTestClock.CmdReg()].ExpectWrite(0x1);

  // Invalid clock ID and bad rate.
  zx_status_t st = clk.ClockImplSetRate(0, 0);
  EXPECT_NE(st, ZX_OK);

  // Set a good clock and a bad rate.
  constexpr uint64_t kBadClkRate = 1;
  st = clk.ClockImplSetRate(msm8x53::kBlsp1Uart2AppsClkSrc, kBadClkRate);
  EXPECT_NE(st, ZX_OK);

  // Try setting a clock that exists.
  const uint64_t kGoodClkRate = kTestFrequency.rate();
  st = clk.ClockImplSetRate(msm8x53::kBlsp1Uart2AppsClkSrc, kGoodClkRate);
  EXPECT_OK(st);

  cc_regs.VerifyAll();
}

// Test Half Integer Divider path.
TEST(ClkTest8x53, TestSetRcgHid) {
  auto cc_regs_arr = std::make_unique<ddk_mock::MockMmioReg[]>(msm8x53::kCcSize);
  ddk_mock::MockMmioRegRegion cc_regs(cc_regs_arr.get(), sizeof(uint32_t), msm8x53::kCcSize);
  Msm8x53ClkTest clk(cc_regs);

  const MsmClkRcg& kTestClock = kMsmClkRcgs[msm8x53::MsmClkIndex(msm8x53::kCsi0pClkSrc)];
  const RcgFrequencyTable kTestFrequency = kTestClock.Table()[0];
  cc_regs[msm8x53::kCsi0pCmdRcgr + 0x04].ExpectWrite(kTestFrequency.predev_parent());
  cc_regs[msm8x53::kCsi0pCmdRcgr + 0x00].ExpectWrite(0x1);

  // Set a good clock and a bad rate.
  constexpr uint64_t kBadClkRate = 1;
  zx_status_t st = clk.ClockImplSetRate(msm8x53::kCsi0pClkSrc, kBadClkRate);
  EXPECT_NE(st, ZX_OK);

  // Try setting a clock that exists.
  const uint64_t kGoodClkRate = kTestFrequency.rate();
  st = clk.ClockImplSetRate(msm8x53::kCsi0pClkSrc, kGoodClkRate);
  EXPECT_OK(st);

  cc_regs.VerifyAll();
}

TEST(ClkTest8x53, TestRcgEnableDisabe) {
  auto cc_regs_arr = std::make_unique<ddk_mock::MockMmioReg[]>(msm8x53::kCcSize);
  ddk_mock::MockMmioRegRegion cc_regs(cc_regs_arr.get(), sizeof(uint32_t), msm8x53::kCcSize);
  Msm8x53ClkTest clk(cc_regs);

  const uint32_t kTestClkId = msm8x53::kBlsp1Qup3SpiAppsClkSrc;
  const MsmClkRcg& kTestClock = kMsmClkRcgs[msm8x53::MsmClkIndex(kTestClkId)];
  const RcgFrequencyTable kTestFrequency = kTestClock.Table()[0];

  // You are prohibited from enabling an RCG before setting the rate.
  zx_status_t st = clk.ClockImplEnable(kTestClkId);
  EXPECT_NE(st, ZX_OK);

  // Okay, set a frequency and try again.
  st = clk.ClockImplSetRate(kTestClkId, kTestFrequency.rate());
  EXPECT_OK(st);

  // Try enabling the RCG again and make sure it works.
  st = clk.ClockImplEnable(kTestClkId);
  EXPECT_OK(st);

  cc_regs[kTestClock.CmdReg()].ExpectWrite(((1 << 1)));
}

TEST(ClkTest8x53, TestGateClkEnableDisable) {
  zx_status_t st;
  auto cc_regs_arr = std::make_unique<ddk_mock::MockMmioReg[]>(msm8x53::kCcSize);
  ddk_mock::MockMmioRegRegion cc_regs(cc_regs_arr.get(), sizeof(uint32_t), msm8x53::kCcSize);
  Msm8x53ClkTest clk(cc_regs);

  constexpr uint32_t kTestClkId = msm8x53::kUsb3PipeClk;

  st = clk.ClockImplEnable(kTestClkId);
  EXPECT_OK(st);

  st = clk.ClockImplDisable(kTestClkId);
  EXPECT_OK(st);
}

TEST(ClkTest8x53, TestBranchClkEnableDisable) {
  zx_status_t st;
  auto cc_regs_arr = std::make_unique<ddk_mock::MockMmioReg[]>(msm8x53::kCcSize);
  ddk_mock::MockMmioRegRegion cc_regs(cc_regs_arr.get(), sizeof(uint32_t), msm8x53::kCcSize);
  Msm8x53ClkTest clk(cc_regs);

  constexpr uint32_t kTestClkId = msm8x53::kBlsp2Qup3I2cAppsClk;
  constexpr uint32_t kTestClkIdx = msm8x53::MsmClkIndex(kTestClkId);
  const struct clk::msm_clk_branch& branch = kMsmClkBranches[kTestClkIdx];

  cc_regs[branch.reg].ExpectWrite(kBranchEnable).ExpectRead(0x0);
  st = clk.ClockImplEnable(kTestClkId);
  EXPECT_OK(st);

  cc_regs[branch.reg].ExpectRead(0x0).ExpectWrite(0x0).ExpectRead(0x80000000);
  st = clk.ClockImplDisable(kTestClkId);
  EXPECT_OK(st);

  cc_regs.VerifyAll();
}

TEST(ClkTest8x53, TestVoterClkEnableDisable) {
  zx_status_t st;
  auto cc_regs_arr = std::make_unique<ddk_mock::MockMmioReg[]>(msm8x53::kCcSize);
  ddk_mock::MockMmioRegRegion cc_regs(cc_regs_arr.get(), sizeof(uint32_t), msm8x53::kCcSize);
  Msm8x53ClkTest clk(cc_regs);

  constexpr uint32_t kTestClkId = msm8x53::kBlsp2AhbClk;

  st = clk.ClockImplEnable(kTestClkId);
  EXPECT_OK(st);

  st = clk.ClockImplDisable(kTestClkId);
  EXPECT_OK(st);
}

}  // namespace clk
