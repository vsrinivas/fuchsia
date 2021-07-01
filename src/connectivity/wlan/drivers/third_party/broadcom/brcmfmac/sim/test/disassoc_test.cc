// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

static constexpr zx::duration kTestDuration = zx::sec(100);
static uint16_t kDisassocReason = 44;
static const common::MacAddr kApBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
static constexpr wlan_ssid_t kApSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
static constexpr wlan_channel_t kApChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
static const common::MacAddr kStaMacAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});

TEST_F(SimTest, Disassoc) {
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

// Verify that we properly track the disconnect mode which indicates if a disconnect was initiated
// by SME or not and what kind of disconnect it is. If this is not properly handled we could end up
// in a state where we are disconnected but SME doesn't know about it.
TEST_F(SimTest, SmeDeauthFollowedByFwDisassoc) {
  simulation::FakeAp ap(env_.get(), kApBssid, kApSsid, kApChannel);

  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kStaMacAddr),
            ZX_OK);

  client_ifc.AssociateWith(ap, zx::sec(1));
  constexpr wlanif_reason_code_t deauth_reason = WLAN_REASON_CODE_LEAVING_NETWORK_DISASSOC;
  // Schedule a deauth from SME
  SCHEDULE_CALL(zx::sec(2), &wlan::brcmfmac::SimInterface::DeauthenticateFrom, &client_ifc,
                kApBssid, deauth_reason);
  // Reassociate
  client_ifc.AssociateWith(ap, zx::sec(3));
  // Schedule a disassocaition from firmware
  constexpr wlanif_reason_code_t disassoc_reason = WLAN_REASON_CODE_UNSPECIFIED_REASON;
  SimFirmware& fw = *device_->GetSim()->sim_fw;
  // Note that this disassociation cannot go through SME, it has to be initiated by firmware so that
  // the disconnect mode tracking is not modified.
  SCHEDULE_CALL(zx::sec(4), &SimFirmware::TriggerFirmwareDisassoc, &fw, disassoc_reason);

  env_->Run(kTestDuration);

  // Make sure associations were successful
  ASSERT_EQ(client_ifc.stats_.assoc_attempts, 2U);
  ASSERT_EQ(client_ifc.stats_.assoc_results.size(), 2U);
  ASSERT_EQ(client_ifc.stats_.assoc_results.front().result_code, WLAN_ASSOC_RESULT_SUCCESS);
  ASSERT_EQ(client_ifc.stats_.assoc_results.back().result_code, WLAN_ASSOC_RESULT_SUCCESS);

  // Make sure disassociation was successful
  EXPECT_EQ(ap.GetNumAssociatedClient(), 0U);

  // Verify that we got the deauth confirmation
  ASSERT_EQ(client_ifc.stats_.deauth_results.size(), 1U);
  const wlanif_deauth_confirm_t& deauth_confirm = client_ifc.stats_.deauth_results.front();
  EXPECT_EQ(0, memcmp(deauth_confirm.peer_sta_address, kApBssid.byte, ETH_ALEN));

  // Verify that we got the disassociation indication, not a confirmation or anything else
  ASSERT_EQ(client_ifc.stats_.disassoc_indications.size(), 1U);
  const wlanif_disassoc_indication_t& disassoc_ind = client_ifc.stats_.disassoc_indications.front();
  EXPECT_EQ(disassoc_ind.reason_code, disassoc_reason);
  EXPECT_EQ(disassoc_ind.locally_initiated, true);
}

}  // namespace wlan::brcmfmac
