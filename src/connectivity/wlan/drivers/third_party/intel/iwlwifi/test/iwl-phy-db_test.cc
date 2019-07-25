// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test iwl-phy-db.c.

#include <stdio.h>

#include "gtest/gtest.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-phy-db.h"
}

namespace wlan {
namespace testing {
namespace {

class IwlPhyDbTest : public ::testing::Test {
 public:
  IwlPhyDbTest() {}
  ~IwlPhyDbTest() {}
};

// Expect the iwl_phy_db_send_all_channel_groups() returns ZX_OK right after init before there is
// no data set yet. This is a regression test to catch a bug involving underflow on the channel
// argument to iwl_phy_db_send_all_channel_groups().
TEST_F(IwlPhyDbTest, TestSendAllChannelGroupsAfterInit) {
  struct iwl_phy_db* phy_db = iwl_phy_db_init(nullptr);
  EXPECT_NE(nullptr, phy_db);

  EXPECT_EQ(ZX_OK, iwl_phy_db_send_all_channel_groups(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, -1));
  EXPECT_EQ(ZX_OK, iwl_phy_db_send_all_channel_groups(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, -1));

  iwl_phy_db_free(phy_db);
}

}  // namespace
}  // namespace testing
}  // namespace wlan
