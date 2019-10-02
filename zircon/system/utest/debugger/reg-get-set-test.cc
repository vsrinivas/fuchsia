// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/hw/debug/arm64.h>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

namespace {

bool HWBreakpointGettersTest() {
  BEGIN_TEST;

  EXPECT_EQ(ARM64_DBGBCR_E_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_PMC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_BAS_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_HMC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_SSC_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_LBN_GET(0), 0);
  EXPECT_EQ(ARM64_DBGBCR_BT_GET(0), 0);

  // Arm64.
  uint32_t dbgbcr = 1 << 0    |   // E = 1
                    3 << 1    |   // PMC = 3
                    5 << 5    |   // BAS = 5
                    1 << 13   |   // HMC = 1
                    3 << 14   |   // SSC = 3
                    14 << 16  |   // LBN = 14
                    15 << 20;     // BT = 15

  EXPECT_EQ(ARM64_DBGBCR_E_GET(dbgbcr), 1);
  EXPECT_EQ(ARM64_DBGBCR_PMC_GET(dbgbcr), 3);
  EXPECT_EQ(ARM64_DBGBCR_BAS_GET(dbgbcr), 5);
  EXPECT_EQ(ARM64_DBGBCR_HMC_GET(dbgbcr), 1);
  EXPECT_EQ(ARM64_DBGBCR_SSC_GET(dbgbcr), 3);
  EXPECT_EQ(ARM64_DBGBCR_LBN_GET(dbgbcr), 14);
  EXPECT_EQ(ARM64_DBGBCR_BT_GET(dbgbcr), 15);

  END_TEST;
}

bool HWBreakpointSettersTest() {
  BEGIN_TEST;

  // Arm64.
  uint32_t dbgbcr = 0;
  uint32_t golden = 1 << 0    |   // E = 1
                    3 << 1    |   // PMC = 3
                    5 << 5    |   // BAS = 5
                    1 << 13   |   // HMC = 1
                    3 << 14   |   // SSC = 3
                    14 << 16  |   // LBN = 14
                    15 << 20;     // BT = 15

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

bool WatchpointGettersTest() {
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

  uint32_t dbgwcr = 1 << 0    |   // E = 1
                    3 << 1    |   // PAC = 3
                    2 << 3    |   // LSC = 2
                    114 << 5  |   // BAS = 114
                    1 << 13   |   // HMC = 1
                    1 << 14   |   // SSC = 1
                    13 << 16  |   // LBN = 13
                    1 << 20   |   // WT = 1
                    27 << 24;     // MSK = 27

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

bool WatchpointSettersTest() {
  BEGIN_TEST;

  uint32_t dbgwcr = 0;
  uint32_t golden = 1 << 0    |   // E = 1
                    3 << 1    |   // PAC = 3
                    2 << 3    |   // LSC = 2
                    114 << 5  |   // BAS = 114
                    1 << 13   |   // HMC = 1
                    1 << 14   |   // SSC = 1
                    13 << 16  |   // LBN = 13
                    1 << 20   |   // WT = 1
                    27 << 24;     // MSK = 27

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

}  // namespace

BEGIN_TEST_CASE(register_get_set_tests)
RUN_TEST(HWBreakpointGettersTest);
RUN_TEST(HWBreakpointSettersTest);
RUN_TEST(WatchpointGettersTest);
RUN_TEST(WatchpointSettersTest);
END_TEST_CASE(register_get_set_tests)
