// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/tx_vector.h>
#include <wlan/mlme/debug.h>

#include <vector>
namespace wlan {
namespace {

TEST(TxVectorIndexTest, TxVectorMapping) {
  struct TestVector {
    TxVector want_vec;
    tx_vec_idx_t want_idx;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 1,  0},   1,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 1,  7},   8,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 2,  8},   9,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 2, 15},  16,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 1,  0},  33,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 1,  7},  40,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 2,  8},  41,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 2, 15},  48,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 1,  0},  65,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 1,  7},  72,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 2,  8},  73,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 2, 15},  80,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 1,  0},  97,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 1,  7}, 104,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 2,  8}, 105,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 2, 15}, 112,},

        {{WLAN_PHY_ERP,  WLAN_GI_800NS, CBW20, 1, 0}, 129,},
        {{WLAN_PHY_ERP,  WLAN_GI_800NS, CBW20, 1, 7}, 136,},
        {{WLAN_PHY_DSSS, WLAN_GI_800NS, CBW20, 1, 0}, 137,},
        {{WLAN_PHY_CCK,  WLAN_GI_800NS, CBW20, 1, 3}, 140,},
      // clang-format on
  };

  for (auto tv : tvs) {
    TxVector got_vec;
    zx_status_t status = TxVector::FromIdx(tv.want_idx, &got_vec);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(tv.want_vec, got_vec);

    tx_vec_idx_t got_idx;
    status = tv.want_vec.ToIdx(&got_idx);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(tv.want_idx, got_idx);

    std::string got_str_from_idx = debug::Describe(tv.want_idx);
    std::string got_str_from_vec = debug::Describe(tv.want_vec);
    EXPECT_EQ(got_str_from_idx, got_str_from_vec);
  }
}

TEST(TxVectorIndexTest, NotUsedParam) {
  struct TestVector {
    TxVector want_vec;
    tx_vec_idx_t want_idx;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        //                                   nss not used
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 0,  0},   1,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 1,  7},   8,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 2,  8},   9,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW20, 3, 15},  16,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 4,  0},  33,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 3,  7},  40,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 2,  8},  41,},
        {{WLAN_PHY_HT, WLAN_GI_800NS, CBW40, 1, 15},  48,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 0,  0},  65,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 1,  7},  72,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 2,  8},  73,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW20, 3, 15},  80,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 4,  0},  97,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 3,  7}, 104,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 2,  8}, 105,},
        {{WLAN_PHY_HT, WLAN_GI_400NS, CBW40, 1, 15}, 112,},

        // only compare phy and mcs_idx
        {{WLAN_PHY_ERP, WLAN_GI_800NS, CBW20,  1, 0}, 129,},
        {{WLAN_PHY_ERP, WLAN_GI_400NS, CBW40,  2, 7}, 136,},
        {{WLAN_PHY_ERP, WLAN_GI_800NS, CBW80,  3, 0}, 129,},
        {{WLAN_PHY_ERP, WLAN_GI_400NS, CBW160, 4, 7}, 136,},
        {{WLAN_PHY_ERP, WLAN_GI_800NS, CBW80,  3, 0}, 129,},
        {{WLAN_PHY_ERP, WLAN_GI_400NS, CBW40,  2, 7}, 136,},
        {{WLAN_PHY_ERP, WLAN_GI_800NS, CBW20,  1, 0}, 129,},
        {{WLAN_PHY_ERP, WLAN_GI_400NS, CBW40,  0, 7}, 136,},
        {{WLAN_PHY_ERP, WLAN_GI_400NS, CBW80,  8, 0}, 129,},
        {{WLAN_PHY_ERP, WLAN_GI_800NS, CBW160, 9, 7}, 136,},

        {{WLAN_PHY_DSSS, WLAN_GI_800NS, CBW20,  1, 0}, 137,},
        {{WLAN_PHY_CCK,  WLAN_GI_400NS, CBW40,  2, 3}, 140,},
        {{WLAN_PHY_DSSS, WLAN_GI_800NS, CBW80,  3, 0}, 137,},
        {{WLAN_PHY_CCK,  WLAN_GI_400NS, CBW160, 4, 3}, 140,},
        {{WLAN_PHY_DSSS, WLAN_GI_800NS, CBW80,  3, 0}, 137,},
        {{WLAN_PHY_CCK,  WLAN_GI_400NS, CBW40,  2, 3}, 140,},
        {{WLAN_PHY_DSSS, WLAN_GI_800NS, CBW20,  1, 0}, 137,},
        {{WLAN_PHY_CCK,  WLAN_GI_400NS, CBW40,  0, 3}, 140,},
        {{WLAN_PHY_DSSS, WLAN_GI_400NS, CBW80,  8, 0}, 137,},
        {{WLAN_PHY_CCK,  WLAN_GI_800NS, CBW160, 9, 3}, 140,},
      // clang-format on
  };

  for (auto tv : tvs) {
    TxVector got_vec;
    zx_status_t status = TxVector::FromIdx(tv.want_idx, &got_vec);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(tv.want_vec, got_vec);

    tx_vec_idx_t got_idx;
    status = tv.want_vec.ToIdx(&got_idx);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(tv.want_idx, got_idx);
  }
}

TEST(TxVectorTest, ValidIdx) {
  std::vector<tx_vec_idx_t> valid_indices = {
      1, 8, 9, 16, 65, 97, /* HT | ERP */ 129, 136};
  for (tx_vec_idx_t want_idx : valid_indices) {
    TxVector got_vec;
    zx_status_t status = TxVector::FromIdx(want_idx, &got_vec);
    EXPECT_EQ(ZX_OK, status);
    tx_vec_idx_t got_idx;
    status = got_vec.ToIdx(&got_idx);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(want_idx, got_idx);
  }
}

TEST(ErpRateTest, ErpRateToTxVector) {
  struct TestVector {
    SupportedRate supported_rate;
    TxVector want_vec;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {SupportedRate(  2), {.phy = WLAN_PHY_DSSS, .mcs_idx = 0},},
        {SupportedRate(  4), {.phy = WLAN_PHY_DSSS, .mcs_idx = 1},},
        {SupportedRate( 11), {.phy = WLAN_PHY_CCK,  .mcs_idx = 2},},
        {SupportedRate( 22), {.phy = WLAN_PHY_CCK,  .mcs_idx = 3},},
        {SupportedRate( 12), {.phy = WLAN_PHY_ERP,  .mcs_idx = 0},},
        {SupportedRate( 18), {.phy = WLAN_PHY_ERP,  .mcs_idx = 1},},
        {SupportedRate( 24), {.phy = WLAN_PHY_ERP,  .mcs_idx = 2},},
        {SupportedRate( 36), {.phy = WLAN_PHY_ERP,  .mcs_idx = 3},},
        {SupportedRate( 48), {.phy = WLAN_PHY_ERP,  .mcs_idx = 4},},
        {SupportedRate( 72), {.phy = WLAN_PHY_ERP,  .mcs_idx = 5},},
        {SupportedRate( 96), {.phy = WLAN_PHY_ERP,  .mcs_idx = 6},},
        {SupportedRate(108), {.phy = WLAN_PHY_ERP,  .mcs_idx = 7},},

        {SupportedRate::basic(  2), {.phy = WLAN_PHY_DSSS, .mcs_idx = 0},},
        {SupportedRate::basic(  4), {.phy = WLAN_PHY_DSSS, .mcs_idx = 1},},
        {SupportedRate::basic( 11), {.phy = WLAN_PHY_CCK,  .mcs_idx = 2},},
        {SupportedRate::basic( 22), {.phy = WLAN_PHY_CCK,  .mcs_idx = 3},},
        {SupportedRate::basic( 12), {.phy = WLAN_PHY_ERP,  .mcs_idx = 0},},
        {SupportedRate::basic( 18), {.phy = WLAN_PHY_ERP,  .mcs_idx = 1},},
        {SupportedRate::basic( 24), {.phy = WLAN_PHY_ERP,  .mcs_idx = 2},},
        {SupportedRate::basic( 36), {.phy = WLAN_PHY_ERP,  .mcs_idx = 3},},
        {SupportedRate::basic( 48), {.phy = WLAN_PHY_ERP,  .mcs_idx = 4},},
        {SupportedRate::basic( 72), {.phy = WLAN_PHY_ERP,  .mcs_idx = 5},},
        {SupportedRate::basic( 96), {.phy = WLAN_PHY_ERP,  .mcs_idx = 6},},
        {SupportedRate::basic(108), {.phy = WLAN_PHY_ERP,  .mcs_idx = 7},},
      // clang-format on
  };

  for (auto tv : tvs) {
    TxVector got_vec;
    zx_status_t status =
        TxVector::FromSupportedRate(tv.supported_rate, &got_vec);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(tv.want_vec, got_vec);
  }
}

TEST(DebugTest, DebugStringVisualInspection) {
  std::string want_str[] = {
      "  1:  HT GI800 CBW20 NSS 1 MCS  0",
      "  2:  HT GI800 CBW20 NSS 1 MCS  1",
      "  3:  HT GI800 CBW20 NSS 1 MCS  2",
      "  4:  HT GI800 CBW20 NSS 1 MCS  3",
      "  5:  HT GI800 CBW20 NSS 1 MCS  4",
      "  6:  HT GI800 CBW20 NSS 1 MCS  5",
      "  7:  HT GI800 CBW20 NSS 1 MCS  6",
      "  8:  HT GI800 CBW20 NSS 1 MCS  7",
      "  9:  HT GI800 CBW20 NSS 2 MCS  8",
      " 10:  HT GI800 CBW20 NSS 2 MCS  9",
      " 11:  HT GI800 CBW20 NSS 2 MCS 10",
      " 12:  HT GI800 CBW20 NSS 2 MCS 11",
      " 13:  HT GI800 CBW20 NSS 2 MCS 12",
      " 14:  HT GI800 CBW20 NSS 2 MCS 13",
      " 15:  HT GI800 CBW20 NSS 2 MCS 14",
      " 16:  HT GI800 CBW20 NSS 2 MCS 15",
      " 17:  HT GI800 CBW20 NSS 3 MCS 16",
      " 18:  HT GI800 CBW20 NSS 3 MCS 17",
      " 19:  HT GI800 CBW20 NSS 3 MCS 18",
      " 20:  HT GI800 CBW20 NSS 3 MCS 19",
      " 21:  HT GI800 CBW20 NSS 3 MCS 20",
      " 22:  HT GI800 CBW20 NSS 3 MCS 21",
      " 23:  HT GI800 CBW20 NSS 3 MCS 22",
      " 24:  HT GI800 CBW20 NSS 3 MCS 23",
      " 25:  HT GI800 CBW20 NSS 4 MCS 24",
      " 26:  HT GI800 CBW20 NSS 4 MCS 25",
      " 27:  HT GI800 CBW20 NSS 4 MCS 26",
      " 28:  HT GI800 CBW20 NSS 4 MCS 27",
      " 29:  HT GI800 CBW20 NSS 4 MCS 28",
      " 30:  HT GI800 CBW20 NSS 4 MCS 29",
      " 31:  HT GI800 CBW20 NSS 4 MCS 30",
      " 32:  HT GI800 CBW20 NSS 4 MCS 31",
      " 33:  HT GI800 CBW40 NSS 1 MCS  0",
      " 34:  HT GI800 CBW40 NSS 1 MCS  1",
      " 35:  HT GI800 CBW40 NSS 1 MCS  2",
      " 36:  HT GI800 CBW40 NSS 1 MCS  3",
      " 37:  HT GI800 CBW40 NSS 1 MCS  4",
      " 38:  HT GI800 CBW40 NSS 1 MCS  5",
      " 39:  HT GI800 CBW40 NSS 1 MCS  6",
      " 40:  HT GI800 CBW40 NSS 1 MCS  7",
      " 41:  HT GI800 CBW40 NSS 2 MCS  8",
      " 42:  HT GI800 CBW40 NSS 2 MCS  9",
      " 43:  HT GI800 CBW40 NSS 2 MCS 10",
      " 44:  HT GI800 CBW40 NSS 2 MCS 11",
      " 45:  HT GI800 CBW40 NSS 2 MCS 12",
      " 46:  HT GI800 CBW40 NSS 2 MCS 13",
      " 47:  HT GI800 CBW40 NSS 2 MCS 14",
      " 48:  HT GI800 CBW40 NSS 2 MCS 15",
      " 49:  HT GI800 CBW40 NSS 3 MCS 16",
      " 50:  HT GI800 CBW40 NSS 3 MCS 17",
      " 51:  HT GI800 CBW40 NSS 3 MCS 18",
      " 52:  HT GI800 CBW40 NSS 3 MCS 19",
      " 53:  HT GI800 CBW40 NSS 3 MCS 20",
      " 54:  HT GI800 CBW40 NSS 3 MCS 21",
      " 55:  HT GI800 CBW40 NSS 3 MCS 22",
      " 56:  HT GI800 CBW40 NSS 3 MCS 23",
      " 57:  HT GI800 CBW40 NSS 4 MCS 24",
      " 58:  HT GI800 CBW40 NSS 4 MCS 25",
      " 59:  HT GI800 CBW40 NSS 4 MCS 26",
      " 60:  HT GI800 CBW40 NSS 4 MCS 27",
      " 61:  HT GI800 CBW40 NSS 4 MCS 28",
      " 62:  HT GI800 CBW40 NSS 4 MCS 29",
      " 63:  HT GI800 CBW40 NSS 4 MCS 30",
      " 64:  HT GI800 CBW40 NSS 4 MCS 31",
      " 65:  HT GI400 CBW20 NSS 1 MCS  0",
      " 66:  HT GI400 CBW20 NSS 1 MCS  1",
      " 67:  HT GI400 CBW20 NSS 1 MCS  2",
      " 68:  HT GI400 CBW20 NSS 1 MCS  3",
      " 69:  HT GI400 CBW20 NSS 1 MCS  4",
      " 70:  HT GI400 CBW20 NSS 1 MCS  5",
      " 71:  HT GI400 CBW20 NSS 1 MCS  6",
      " 72:  HT GI400 CBW20 NSS 1 MCS  7",
      " 73:  HT GI400 CBW20 NSS 2 MCS  8",
      " 74:  HT GI400 CBW20 NSS 2 MCS  9",
      " 75:  HT GI400 CBW20 NSS 2 MCS 10",
      " 76:  HT GI400 CBW20 NSS 2 MCS 11",
      " 77:  HT GI400 CBW20 NSS 2 MCS 12",
      " 78:  HT GI400 CBW20 NSS 2 MCS 13",
      " 79:  HT GI400 CBW20 NSS 2 MCS 14",
      " 80:  HT GI400 CBW20 NSS 2 MCS 15",
      " 81:  HT GI400 CBW20 NSS 3 MCS 16",
      " 82:  HT GI400 CBW20 NSS 3 MCS 17",
      " 83:  HT GI400 CBW20 NSS 3 MCS 18",
      " 84:  HT GI400 CBW20 NSS 3 MCS 19",
      " 85:  HT GI400 CBW20 NSS 3 MCS 20",
      " 86:  HT GI400 CBW20 NSS 3 MCS 21",
      " 87:  HT GI400 CBW20 NSS 3 MCS 22",
      " 88:  HT GI400 CBW20 NSS 3 MCS 23",
      " 89:  HT GI400 CBW20 NSS 4 MCS 24",
      " 90:  HT GI400 CBW20 NSS 4 MCS 25",
      " 91:  HT GI400 CBW20 NSS 4 MCS 26",
      " 92:  HT GI400 CBW20 NSS 4 MCS 27",
      " 93:  HT GI400 CBW20 NSS 4 MCS 28",
      " 94:  HT GI400 CBW20 NSS 4 MCS 29",
      " 95:  HT GI400 CBW20 NSS 4 MCS 30",
      " 96:  HT GI400 CBW20 NSS 4 MCS 31",
      " 97:  HT GI400 CBW40 NSS 1 MCS  0",
      " 98:  HT GI400 CBW40 NSS 1 MCS  1",
      " 99:  HT GI400 CBW40 NSS 1 MCS  2",
      "100:  HT GI400 CBW40 NSS 1 MCS  3",
      "101:  HT GI400 CBW40 NSS 1 MCS  4",
      "102:  HT GI400 CBW40 NSS 1 MCS  5",
      "103:  HT GI400 CBW40 NSS 1 MCS  6",
      "104:  HT GI400 CBW40 NSS 1 MCS  7",
      "105:  HT GI400 CBW40 NSS 2 MCS  8",
      "106:  HT GI400 CBW40 NSS 2 MCS  9",
      "107:  HT GI400 CBW40 NSS 2 MCS 10",
      "108:  HT GI400 CBW40 NSS 2 MCS 11",
      "109:  HT GI400 CBW40 NSS 2 MCS 12",
      "110:  HT GI400 CBW40 NSS 2 MCS 13",
      "111:  HT GI400 CBW40 NSS 2 MCS 14",
      "112:  HT GI400 CBW40 NSS 2 MCS 15",
      "113:  HT GI400 CBW40 NSS 3 MCS 16",
      "114:  HT GI400 CBW40 NSS 3 MCS 17",
      "115:  HT GI400 CBW40 NSS 3 MCS 18",
      "116:  HT GI400 CBW40 NSS 3 MCS 19",
      "117:  HT GI400 CBW40 NSS 3 MCS 20",
      "118:  HT GI400 CBW40 NSS 3 MCS 21",
      "119:  HT GI400 CBW40 NSS 3 MCS 22",
      "120:  HT GI400 CBW40 NSS 3 MCS 23",
      "121:  HT GI400 CBW40 NSS 4 MCS 24",
      "122:  HT GI400 CBW40 NSS 4 MCS 25",
      "123:  HT GI400 CBW40 NSS 4 MCS 26",
      "124:  HT GI400 CBW40 NSS 4 MCS 27",
      "125:  HT GI400 CBW40 NSS 4 MCS 28",
      "126:  HT GI400 CBW40 NSS 4 MCS 29",
      "127:  HT GI400 CBW40 NSS 4 MCS 30",
      "128:  HT GI400 CBW40 NSS 4 MCS 31",
      "129: ERP GI800 CBW20 NSS 1 MCS  0",
      "130: ERP GI800 CBW20 NSS 1 MCS  1",
      "131: ERP GI800 CBW20 NSS 1 MCS  2",
      "132: ERP GI800 CBW20 NSS 1 MCS  3",
      "133: ERP GI800 CBW20 NSS 1 MCS  4",
      "134: ERP GI800 CBW20 NSS 1 MCS  5",
      "135: ERP GI800 CBW20 NSS 1 MCS  6",
      "136: ERP GI800 CBW20 NSS 1 MCS  7",
      "137: DSSS GI800 CBW20 NSS 1 MCS  0",
      "138: DSSS GI800 CBW20 NSS 1 MCS  1",
      "139: CCK GI800 CBW20 NSS 1 MCS  2",
      "140: CCK GI800 CBW20 NSS 1 MCS  3",
  };
  for (tx_vec_idx_t i = 1; i <= 140; ++i) {
    EXPECT_EQ(want_str[i - 1], debug::Describe(i));
  }
}

}  // namespace
}  // namespace wlan