// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/hw/debug/arm64.h>
#include <zircon/hw/debug/x86.h>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

namespace {

bool Arm64HWBreakpointGettersTest() {
  BEGIN_TEST;

  EXPECT_EQ(ARM64_DBGBCR_E_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_PMC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_BAS_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_HMC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_SSC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_LBN_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_BT_GET(0), 0);

  // Arm64.
  uint32_t dbgbcr = 1u << 0 |    // E = 1
                    3u << 1 |    // PMC = 3
                    5u << 5 |    // BAS = 5
                    1u << 13 |   // HMC = 1
                    3u << 14 |   // SSC = 3
                    14u << 16 |  // LBN = 14
                    15u << 20;   // BT = 15

  EXPECT_EQ(ARM64_DBGBCR_E_GET(dbgbcr), 1);
  EXPECT_EQ(ARM64_DBGBCR_PMC_GET(dbgbcr), 3);
  EXPECT_EQ(ARM64_DBGBCR_BAS_GET(dbgbcr), 5);
  EXPECT_EQ(ARM64_DBGBCR_HMC_GET(dbgbcr), 1);
  EXPECT_EQ(ARM64_DBGBCR_SSC_GET(dbgbcr), 3);
  EXPECT_EQ(ARM64_DBGBCR_LBN_GET(dbgbcr), 14);
  EXPECT_EQ(ARM64_DBGBCR_BT_GET(dbgbcr), 15);

  END_TEST;
}

bool Arm64HWBreakpointSettersTest() {
  BEGIN_TEST;

  // Arm64.
  uint32_t dbgbcr = 0;
  uint32_t golden = 1u << 0 |    // E = 1
                    3u << 1 |    // PMC = 3
                    5u << 5 |    // BAS = 5
                    1u << 13 |   // HMC = 1
                    3u << 14 |   // SSC = 3
                    14u << 16 |  // LBN = 14
                    15u << 20;   // BT = 15

  ARM64_DBGBCR_E_SET(&dbgbcr, 1);
  ARM64_DBGBCR_PMC_SET(&dbgbcr, 3);
  ARM64_DBGBCR_BAS_SET(&dbgbcr, 5);
  ARM64_DBGBCR_HMC_SET(&dbgbcr, 1);
  ARM64_DBGBCR_SSC_SET(&dbgbcr, 3);
  ARM64_DBGBCR_LBN_SET(&dbgbcr, 14);
  ARM64_DBGBCR_BT_SET(&dbgbcr, 15);

  EXPECT_EQ(dbgbcr, golden);
  EXPECT_EQ(ARM64_DBGBCR_E_GET(dbgbcr), 1);
  EXPECT_EQ(ARM64_DBGBCR_PMC_GET(dbgbcr), 3);
  EXPECT_EQ(ARM64_DBGBCR_BAS_GET(dbgbcr), 5);
  EXPECT_EQ(ARM64_DBGBCR_HMC_GET(dbgbcr), 1);
  EXPECT_EQ(ARM64_DBGBCR_SSC_GET(dbgbcr), 3);
  EXPECT_EQ(ARM64_DBGBCR_LBN_GET(dbgbcr), 14);
  EXPECT_EQ(ARM64_DBGBCR_BT_GET(dbgbcr), 15);

  END_TEST;
}

bool Arm64WatchpointGettersTest() {
  BEGIN_TEST;

  // Arm64.
  EXPECT_EQ(ARM64_DBGWCR_E_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_PAC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_LSC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_BAS_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_HMC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_SSC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_LBN_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_WT_GET(0), 0);
  EXPECT_EQ(ARM64_DBGWCR_MSK_GET(0), 0);

  uint32_t dbgwcr = 1u << 0 |    // E = 1
                    3u << 1 |    // PAC = 3
                    2u << 3 |    // LSC = 2
                    114u << 5 |  // BAS = 114
                    1u << 13 |   // HMC = 1
                    1u << 14 |   // SSC = 1
                    13u << 16 |  // LBN = 13
                    1u << 20 |   // WT = 1
                    27u << 24;   // MSK = 27

  EXPECT_EQ(ARM64_DBGWCR_E_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_PAC_GET(dbgwcr), 3);
  EXPECT_EQ(ARM64_DBGWCR_LSC_GET(dbgwcr), 2);
  EXPECT_EQ(ARM64_DBGWCR_BAS_GET(dbgwcr), 114);
  EXPECT_EQ(ARM64_DBGWCR_HMC_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_SSC_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_LBN_GET(dbgwcr), 13);
  EXPECT_EQ(ARM64_DBGWCR_WT_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_MSK_GET(dbgwcr), 27);

  END_TEST;
}

bool Arm64WatchpointSettersTest() {
  BEGIN_TEST;

  uint32_t dbgwcr = 0;
  uint32_t golden = 1u << 0 |    // E = 1
                    3u << 1 |    // PAC = 3
                    2u << 3 |    // LSC = 2
                    114u << 5 |  // BAS = 114
                    1u << 13 |   // HMC = 1
                    1u << 14 |   // SSC = 1
                    13u << 16 |  // LBN = 13
                    1u << 20 |   // WT = 1
                    27u << 24;   // MSK = 27

  ARM64_DBGWCR_E_SET(&dbgwcr, 1);
  ARM64_DBGWCR_PAC_SET(&dbgwcr, 3);
  ARM64_DBGWCR_LSC_SET(&dbgwcr, 2);
  ARM64_DBGWCR_BAS_SET(&dbgwcr, 114);
  ARM64_DBGWCR_HMC_SET(&dbgwcr, 1);
  ARM64_DBGWCR_SSC_SET(&dbgwcr, 1);
  ARM64_DBGWCR_LBN_SET(&dbgwcr, 13);
  ARM64_DBGWCR_WT_SET(&dbgwcr, 1);
  ARM64_DBGWCR_MSK_SET(&dbgwcr, 27);

  EXPECT_EQ(dbgwcr, golden);
  EXPECT_EQ(ARM64_DBGWCR_E_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_PAC_GET(dbgwcr), 3);
  EXPECT_EQ(ARM64_DBGWCR_LSC_GET(dbgwcr), 2);
  EXPECT_EQ(ARM64_DBGWCR_BAS_GET(dbgwcr), 114);
  EXPECT_EQ(ARM64_DBGWCR_HMC_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_SSC_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_LBN_GET(dbgwcr), 13);
  EXPECT_EQ(ARM64_DBGWCR_WT_GET(dbgwcr), 1);
  EXPECT_EQ(ARM64_DBGWCR_MSK_GET(dbgwcr), 27);

  END_TEST;
}

bool x86DR6GetTest() {
  BEGIN_TEST;

  EXPECT_EQ(X86_DBG_STATUS_B0_GET(0), 0);
  EXPECT_EQ(X86_DBG_STATUS_B1_GET(0), 0);
  EXPECT_EQ(X86_DBG_STATUS_B2_GET(0), 0);
  EXPECT_EQ(X86_DBG_STATUS_B3_GET(0), 0);
  EXPECT_EQ(X86_DBG_STATUS_BD_GET(0), 0);
  EXPECT_EQ(X86_DBG_STATUS_BS_GET(0), 0);
  EXPECT_EQ(X86_DBG_STATUS_BT_GET(0), 0);

  uint32_t dr6 = 1u << 0 |   // B0 = 1
                 1u << 2 |   // B2 = 1
                 1u << 13 |  // BD = 1
                 1u << 15;   // DT = 1

  EXPECT_EQ(X86_DBG_STATUS_B0_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_B1_GET(dr6), 0);
  EXPECT_EQ(X86_DBG_STATUS_B2_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_B3_GET(dr6), 0);
  EXPECT_EQ(X86_DBG_STATUS_BD_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_BS_GET(dr6), 0);
  EXPECT_EQ(X86_DBG_STATUS_BT_GET(dr6), 1);

  END_TEST;
}

bool x86DR6SetTest() {
  BEGIN_TEST;

  uint64_t dr6 = 0;
  uint64_t golden = 1u << 0 |   // B0 = 1
                    1u << 1 |   // B1 = 1
                    1u << 2 |   // B2 = 1
                    1u << 3 |   // B3 = 1
                    1u << 13 |  // BD = 1
                    1u << 14 |  // BS = 1
                    1u << 15;   // DT = 1

  X86_DBG_STATUS_B0_SET(&dr6, 1);
  X86_DBG_STATUS_B1_SET(&dr6, 1);
  X86_DBG_STATUS_B2_SET(&dr6, 1);
  X86_DBG_STATUS_B3_SET(&dr6, 1);
  X86_DBG_STATUS_BD_SET(&dr6, 1);
  X86_DBG_STATUS_BS_SET(&dr6, 1);
  X86_DBG_STATUS_BT_SET(&dr6, 1);

  EXPECT_EQ(dr6, golden);
  EXPECT_EQ(X86_DBG_STATUS_B0_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_B1_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_B2_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_B3_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_BD_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_BS_GET(dr6), 1);
  EXPECT_EQ(X86_DBG_STATUS_BT_GET(dr6), 1);

  END_TEST;
}

bool x86DR7GetTest() {
  BEGIN_TEST;

  EXPECT_EQ(X86_DBG_CONTROL_L0_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_G0_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_L1_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_G1_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_L2_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_G2_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_L3_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_G3_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LE_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_GE_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_GD_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_RW0_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LEN0_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_RW1_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LEN1_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_RW2_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LEN2_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_RW3_GET(0), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LEN3_GET(0), 0);

  uint32_t dr7 = 1u << 0 |   // L0 = 1
                 1u << 2 |   // L1 = 1
                 1u << 5 |   // G2 = 1
                 1u << 6 |   // L3 = 1
                 1u << 8 |   // LE = 1
                 1u << 13 |  // GD = 1
                 1u << 16 |  // RW0 = 1
                 1u << 18 |  // LEN0 = 1
                 2u << 20 |  // RW1 = 2
                 2u << 22 |  // LEN1 = 2
                 3u << 24 |  // RW2 = 3
                 3u << 26;   // RW2 = 3

  EXPECT_EQ(X86_DBG_CONTROL_L0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G0_GET(dr7), 0);
  EXPECT_EQ(X86_DBG_CONTROL_L1_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G1_GET(dr7), 0);
  EXPECT_EQ(X86_DBG_CONTROL_L2_GET(dr7), 0);
  EXPECT_EQ(X86_DBG_CONTROL_G2_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_L3_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G3_GET(dr7), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LE_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_GE_GET(dr7), 0);
  EXPECT_EQ(X86_DBG_CONTROL_GD_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_RW0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_LEN0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_RW1_GET(dr7), 2);
  EXPECT_EQ(X86_DBG_CONTROL_LEN1_GET(dr7), 2);
  EXPECT_EQ(X86_DBG_CONTROL_RW2_GET(dr7), 3);
  EXPECT_EQ(X86_DBG_CONTROL_LEN2_GET(dr7), 3);
  EXPECT_EQ(X86_DBG_CONTROL_RW3_GET(dr7), 0);
  EXPECT_EQ(X86_DBG_CONTROL_LEN3_GET(dr7), 0);

  END_TEST;
}

bool x86DR7SetTest() {
  BEGIN_TEST;

  uint64_t dr7 = 0;
  uint64_t golden = 1u << 0 |   // L0 = 1
                    1u << 1 |   // G0 = 1
                    1u << 2 |   // L1 = 1
                    1u << 3 |   // G1 = 1
                    1u << 4 |   // L2 = 1
                    1u << 5 |   // G2 = 1
                    1u << 6 |   // L3 = 1
                    1u << 7 |   // G3 = 1
                    1u << 8 |   // GE = 1
                    1u << 9 |   // GD = 1
                    1u << 13 |  // GD = 1
                    1u << 16 |  // RW0 = 1
                    1u << 18 |  // LEN0 = 1
                    2u << 20 |  // RW1 = 2
                    2u << 22 |  // LEN1 = 2
                    3u << 24 |  // RW2 = 3
                    3u << 26 |  // RW2 = 3
                    1u << 28 |  // RW3 = 1
                    2u << 30;   // RW3 = 2

  X86_DBG_CONTROL_L0_SET(&dr7, 1);
  X86_DBG_CONTROL_G0_SET(&dr7, 1);
  X86_DBG_CONTROL_L1_SET(&dr7, 1);
  X86_DBG_CONTROL_G1_SET(&dr7, 1);
  X86_DBG_CONTROL_L2_SET(&dr7, 1);
  X86_DBG_CONTROL_G2_SET(&dr7, 1);
  X86_DBG_CONTROL_L3_SET(&dr7, 1);
  X86_DBG_CONTROL_G3_SET(&dr7, 1);
  X86_DBG_CONTROL_LE_SET(&dr7, 1);
  X86_DBG_CONTROL_GE_SET(&dr7, 1);
  X86_DBG_CONTROL_GD_SET(&dr7, 1);
  X86_DBG_CONTROL_RW0_SET(&dr7, 1);
  X86_DBG_CONTROL_LEN0_SET(&dr7, 1);
  X86_DBG_CONTROL_RW1_SET(&dr7, 2);
  X86_DBG_CONTROL_LEN1_SET(&dr7, 2);
  X86_DBG_CONTROL_RW2_SET(&dr7, 3);
  X86_DBG_CONTROL_LEN2_SET(&dr7, 3);
  X86_DBG_CONTROL_RW3_SET(&dr7, 1);
  X86_DBG_CONTROL_LEN3_SET(&dr7, 2);

  EXPECT_EQ(dr7, golden);
  EXPECT_EQ(X86_DBG_CONTROL_L0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_L1_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G1_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_L2_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G2_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_L3_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_G3_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_LE_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_GE_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_GD_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_RW0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_LEN0_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_RW1_GET(dr7), 2);
  EXPECT_EQ(X86_DBG_CONTROL_LEN1_GET(dr7), 2);
  EXPECT_EQ(X86_DBG_CONTROL_RW2_GET(dr7), 3);
  EXPECT_EQ(X86_DBG_CONTROL_LEN2_GET(dr7), 3);
  EXPECT_EQ(X86_DBG_CONTROL_RW3_GET(dr7), 1);
  EXPECT_EQ(X86_DBG_CONTROL_LEN3_GET(dr7), 2);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(register_get_set_tests)
RUN_TEST(Arm64HWBreakpointGettersTest);
RUN_TEST(Arm64HWBreakpointSettersTest);
RUN_TEST(Arm64WatchpointGettersTest);
RUN_TEST(Arm64WatchpointSettersTest);
RUN_TEST(x86DR6GetTest);
RUN_TEST(x86DR6SetTest);
RUN_TEST(x86DR7GetTest);
RUN_TEST(x86DR7SetTest);
END_TEST_CASE(register_get_set_tests)
