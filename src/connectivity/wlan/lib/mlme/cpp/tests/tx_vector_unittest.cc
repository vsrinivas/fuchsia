// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <gtest/gtest.h>
#include <wlan/common/tx_vector.h>
namespace wlan {
namespace {

TEST(TxVectorIndexTest, TxVectorMapping) {
  struct TestVector {
    TxVector want_vec;
    tx_vec_idx_t want_idx;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1,  0},   1,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1,  7},   8,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 2,  8},   9,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 2, 15},  16,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 1,  0},  33,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 1,  7},  40,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 2,  8},  41,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 2, 15},  48,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 1,  0},  65,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 1,  7},  72,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 2,  8},  73,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 2, 15},  80,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 1,  0},  97,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 1,  7}, 104,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 2,  8}, 105,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 2, 15}, 112,},

        {{WLAN_INFO_PHY_TYPE_ERP,  WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1, 0}, 129,},
        {{WLAN_INFO_PHY_TYPE_ERP,  WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1, 7}, 136,},
        {{WLAN_INFO_PHY_TYPE_DSSS, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1, 0}, 137,},
        {{WLAN_INFO_PHY_TYPE_CCK,  WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1, 3}, 140,},
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

TEST(TxVectorIndexTest, NotUsedParam) {
  struct TestVector {
    TxVector want_vec;
    tx_vec_idx_t want_idx;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        //                                   nss not used
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 0,  0},   1,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 1,  7},   8,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 2,  8},   9,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20, 3, 15},  16,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 4,  0},  33,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 3,  7},  40,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 2,  8},  41,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__40, 1, 15},  48,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 0,  0},  65,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 1,  7},  72,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 2,  8},  73,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__20, 3, 15},  80,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 4,  0},  97,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 3,  7}, 104,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 2,  8}, 105,},
        {{WLAN_INFO_PHY_TYPE_HT, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40, 1, 15}, 112,},

        // only compare phy and mcs_idx
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20,  1, 0}, 129,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40,  2, 7}, 136,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__80,  3, 0}, 129,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__160, 4, 7}, 136,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__80,  3, 0}, 129,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40,  2, 7}, 136,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20,  1, 0}, 129,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40,  0, 7}, 136,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__80,  8, 0}, 129,},
        {{WLAN_INFO_PHY_TYPE_ERP, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__160, 9, 7}, 136,},

        {{WLAN_INFO_PHY_TYPE_DSSS, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20,  1, 0}, 137,},
        {{WLAN_INFO_PHY_TYPE_CCK,  WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40,  2, 3}, 140,},
        {{WLAN_INFO_PHY_TYPE_DSSS, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__80,  3, 0}, 137,},
        {{WLAN_INFO_PHY_TYPE_CCK,  WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__160, 4, 3}, 140,},
        {{WLAN_INFO_PHY_TYPE_DSSS, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__80,  3, 0}, 137,},
        {{WLAN_INFO_PHY_TYPE_CCK,  WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40,  2, 3}, 140,},
        {{WLAN_INFO_PHY_TYPE_DSSS, WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__20,  1, 0}, 137,},
        {{WLAN_INFO_PHY_TYPE_CCK,  WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__40,  0, 3}, 140,},
        {{WLAN_INFO_PHY_TYPE_DSSS, WLAN_GI__400NS, WLAN_CHANNEL_BANDWIDTH__80,  8, 0}, 137,},
        {{WLAN_INFO_PHY_TYPE_CCK,  WLAN_GI__800NS, WLAN_CHANNEL_BANDWIDTH__160, 9, 3}, 140,},
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
  std::vector<tx_vec_idx_t> valid_indices = {1, 8, 9, 16, 65, 97, /* HT | ERP */ 129, 136};
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
        {SupportedRate(  2), {.phy = WLAN_INFO_PHY_TYPE_DSSS, .mcs_idx = 0},},
        {SupportedRate(  4), {.phy = WLAN_INFO_PHY_TYPE_DSSS, .mcs_idx = 1},},
        {SupportedRate( 11), {.phy = WLAN_INFO_PHY_TYPE_CCK,  .mcs_idx = 2},},
        {SupportedRate( 22), {.phy = WLAN_INFO_PHY_TYPE_CCK,  .mcs_idx = 3},},
        {SupportedRate( 12), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 0},},
        {SupportedRate( 18), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 1},},
        {SupportedRate( 24), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 2},},
        {SupportedRate( 36), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 3},},
        {SupportedRate( 48), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 4},},
        {SupportedRate( 72), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 5},},
        {SupportedRate( 96), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 6},},
        {SupportedRate(108), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 7},},

        {SupportedRate::basic(  2), {.phy = WLAN_INFO_PHY_TYPE_DSSS, .mcs_idx = 0},},
        {SupportedRate::basic(  4), {.phy = WLAN_INFO_PHY_TYPE_DSSS, .mcs_idx = 1},},
        {SupportedRate::basic( 11), {.phy = WLAN_INFO_PHY_TYPE_CCK,  .mcs_idx = 2},},
        {SupportedRate::basic( 22), {.phy = WLAN_INFO_PHY_TYPE_CCK,  .mcs_idx = 3},},
        {SupportedRate::basic( 12), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 0},},
        {SupportedRate::basic( 18), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 1},},
        {SupportedRate::basic( 24), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 2},},
        {SupportedRate::basic( 36), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 3},},
        {SupportedRate::basic( 48), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 4},},
        {SupportedRate::basic( 72), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 5},},
        {SupportedRate::basic( 96), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 6},},
        {SupportedRate::basic(108), {.phy = WLAN_INFO_PHY_TYPE_ERP,  .mcs_idx = 7},},
      // clang-format on
  };

  for (auto tv : tvs) {
    TxVector got_vec;
    zx_status_t status = TxVector::FromSupportedRate(tv.supported_rate, &got_vec);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(tv.want_vec, got_vec);
  }
}

}  // namespace
}  // namespace wlan
