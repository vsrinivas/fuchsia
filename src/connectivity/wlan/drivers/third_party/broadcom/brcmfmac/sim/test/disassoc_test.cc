// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

TEST_F(SimTest, Disassoc) {
  constexpr zx::duration kTestDuration = zx::sec(100);
  uint16_t kDisassocReason = 44;
  const common::MacAddr kApBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
  constexpr wlan_ssid_t kApSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
  constexpr wlan_channel_t kApChannel = {
      .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
  const common::MacAddr kStaMacAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});

  simulation::FakeAp ap(env_.get(), kApBssid, kApSsid, kApChannel);

  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kStaMacAddr),
            ZX_OK);

  client_ifc.AssociateWith(ap, zx::sec(1));
  SCHEDULE_CALL(zx::sec(2), &simulation::FakeAp::DisassocSta, &ap, kStaMacAddr, kDisassocReason);

  env_->Run(kTestDuration);

  // Make sure association was successful
  ASSERT_EQ(client_ifc.stats_.assoc_attempts, 1U);
  ASSERT_EQ(client_ifc.stats_.assoc_results.size(), 1U);
  ASSERT_EQ(client_ifc.stats_.assoc_results.front().result_code, WLAN_ASSOC_RESULT_SUCCESS);

  // Make sure disassociation was successful
  EXPECT_EQ(ap.GetNumAssociatedClient(), 0U);

  // Verify that we get appropriate notification
  ASSERT_EQ(client_ifc.stats_.disassoc_indications.size(), 1U);
  const wlanif_disassoc_indication_t& disassoc_ind = client_ifc.stats_.disassoc_indications.front();
  // Verify reason code is propagated
  EXPECT_EQ(disassoc_ind.reason_code, kDisassocReason);
  // Disassociated by AP so not locally initiated
  EXPECT_EQ(disassoc_ind.locally_initiated, false);
}

}  // namespace wlan::brcmfmac
