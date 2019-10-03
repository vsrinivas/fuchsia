// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test mvm/phy-ctxt.c

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace wlan {
namespace testing {
namespace {

class PhyContextTest : public ::zxtest::Test {
 public:
  PhyContextTest() {}
  ~PhyContextTest() {}
};

TEST_F(PhyContextTest, GetControlPosition) {
  wlan_channel_t chandef{};

  // Invalid channels. Expect the default value.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__20;
  chandef.primary = 0;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 34;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 68;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 96;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 165;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 255;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);

  // 2.4GHz channels. Expect the default value.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__20;
  chandef.primary = 1;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 14;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);

  // 5GHz channels. But different bandwitdh.

  // 20Mhz primary channels. Expect the default value.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__20;
  chandef.primary = 36;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 64;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 100;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 128;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 132;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 149;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 157;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 161;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);

  // HT40+ primary channels.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__40ABOVE;
  chandef.primary = 36;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 40;  // not allowed
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 44;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 48;  // not allowed
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  // Try channel 149~161 group.
  chandef.primary = 149;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 161;  // not allowed
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);

  // HT40- primary channels.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__40BELOW;
  chandef.primary = 36;  // invalid case since secondary cannot be below 36.
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 40;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_ABOVE);
  chandef.primary = 44;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 48;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_ABOVE);
  chandef.primary = 52;  // invalid case since channel 52 cannot do HT40-.
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  // Try channel 100~128 group.
  chandef.primary = 100;  // invalid case since channel 100 cannot do HT40-.
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 128;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_ABOVE);

  // 80Mhz primary channels.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__80;
  chandef.primary = 36;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_2_BELOW);
  chandef.primary = 40;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 44;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_ABOVE);
  chandef.primary = 48;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_2_ABOVE);

  // 160Mhz primary channels.
  chandef.cbw = WLAN_CHANNEL_BANDWIDTH__160;
  chandef.primary = 36;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_4_BELOW);
  chandef.primary = 40;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_3_BELOW);
  chandef.primary = 44;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_2_BELOW);
  chandef.primary = 48;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 52;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_ABOVE);
  chandef.primary = 56;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_2_ABOVE);
  chandef.primary = 60;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_3_ABOVE);
  chandef.primary = 64;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_4_ABOVE);
  // channel 132+ doesn't support 160Mhz channel. Use default value.
  chandef.primary = 140;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
  chandef.primary = 153;
  EXPECT_EQ(iwl_mvm_get_ctrl_pos(&chandef), PHY_VHT_CTRL_POS_1_BELOW);
}

}  // namespace
}  // namespace testing
}  // namespace wlan
